#include "buffer.h"
#include "stdinc.h"
#include "errors.h"
#include "scope.h"
#include "decoder.h"
#include "jpeg.h"

#include <sched.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

enum {
	thousand = 1000,
	million = thousand * thousand,
	billion = thousand * million,
	horizontal_resolution = 720,
	line_ns = 64000,
	front_porch_ns = 1650,
	back_porch_ns = 5700,
	line_data_ns = line_ns - (back_porch_ns + front_porch_ns),
	oversampling = 1,  // Greater than 1 requires USB3 conection
	sample_rate = oversampling * horizontal_resolution * 1LL * billion / line_data_ns,
	target_sample_period_ns = billion / sample_rate,
	chunk_length = sample_rate / 50,
	poll_interval_us = target_sample_period_ns * chunk_length / 1000 / 4,
	offset_mv = 0,
	frame_width = 720,
	frame_height = 625,
	jpeg_quality = 85,
	metrics_period_s = 5,
};

#define DUTY_U8(state_duration, pulse_duration) ((state_duration) * 256 / (pulse_duration))

/* For the weird chinese camera, all pretty standard values */
/* With a lot of help from http://martin.hinner.info/vga/pal.html */
static struct decoder_config decoder_config = {
	.sample_period_ns = 0,  // Calculated when initialising scope
	.interlaced = true,
	.frame_width = frame_width,
	.frame_height = frame_height,
	.sync_threshold = 200 + offset_mv,
	.black_level = 300 + offset_mv,
	.white_level = 1000 + offset_mv,
	.max_backlog_samples = sample_rate / 4, // Must be longer than 2x frame duration
	.sync_duration_ns = line_ns / 2,
	.line_duration_ns = line_ns,
	.equaliser_low_ns = 2350,
	.vertical_sync_low_ns = line_ns / 2 - 4700,
	.horizontal_sync_low_ns = 4700,
	.front_porch_ns = front_porch_ns,
	.back_porch_ns = back_porch_ns,
	.tolerance_ns = 250,
};

static int ending;

static struct scope scope;

static pthread_mutex_t mutex;

static pthread_cond_t analog_signal_cond;
static struct buffer analog_signal;

static pthread_cond_t image_frames_cond;
static struct buffer image_frames;

static offset_t frame_counter;
static struct decoder_errors decoder_errors;

static pthread_t worker_receiver;
static pthread_t worker_decoder;
static pthread_t worker_image_encoder;

static bool is_not_ending()
{
	struct pollfd pollfd = {
		.fd = ending,
		.events = POLLIN,
	};
	int poll_result = poll(&pollfd, 1, 0);
	assert_equal(true, poll_result >= 0);
	return !(pollfd.revents & POLLIN);
}

static void set_ending(const char *reason)
{
	uint64_t value = 1;
	write(ending, &value, sizeof(&value));
	log("Exiting: %s", reason);
	pthread_cond_signal(&analog_signal_cond);
	pthread_cond_signal(&image_frames_cond);
}

static void *start_worker(void *arg)
{
	void (*entry_point)() = arg;
	entry_point();
	return NULL;
}

void run_receiver()
{
	while (is_not_ending()) {
		bool has_data;
		has_data = scope_capture(&scope);
		if (has_data) {
			pthread_mutex_lock(&mutex);
			has_data = scope_take_data(&scope, &analog_signal);
			if (has_data) {
				pthread_cond_signal(&analog_signal_cond);
			}
			pthread_mutex_unlock(&mutex);
		}
		if (!has_data) {
			usleep(poll_interval_us);
			continue;
		}
	}
	pthread_cond_signal(&analog_signal_cond);
}

void run_decoder()
{
	struct decoder decoder;
	decoder_init(&decoder, &decoder_config);
	const uint32_t frame_bytes = decoder_config.frame_width * decoder_config.frame_height;
	while (is_not_ending()) {
		pthread_mutex_lock(&mutex);
		while (buffer_is_empty(&analog_signal) && is_not_ending()) {
			pthread_cond_wait(&analog_signal_cond, &mutex);
		}
		decoder_bind_and_steal(&decoder, &analog_signal);
		struct timespec now;
		assert_equal(0, clock_gettime(CLOCK_MONOTONIC, &now));
		decoder_reset_error_counters(&decoder, &decoder_errors);
		pthread_mutex_unlock(&mutex);
		while (decoder_read_frame(&decoder)) {
			pthread_mutex_lock(&mutex);
			struct buffer_chunk *frame = buffer_append(&image_frames, frame_bytes);
			frame->offset = frame_counter++;
			memcpy(frame->data, decoder.frame, frame_bytes);
			pthread_cond_signal(&image_frames_cond);
			pthread_mutex_unlock(&mutex);
		}
	}
	decoder_destroy(&decoder);
	pthread_cond_signal(&image_frames_cond);
}

void run_image_encoder()
{
	struct buffer frames;
	buffer_init(&frames);
	while (is_not_ending()) {
			pthread_mutex_lock(&mutex);
			while (buffer_is_empty(&image_frames) && is_not_ending()) {
				pthread_cond_wait(&image_frames_cond, &mutex);
			}
			buffer_concatenate(&frames, &image_frames);
			pthread_mutex_unlock(&mutex);
			if (isatty(STDOUT_FILENO)) {
				log("Frame decoded!");
			} else {
				struct buffer_chunk *frame = frames.tail;
				while (frame) {
					if (!jpeg_write_image(stdout, frame_width, frame_height, false, frame->data, jpeg_quality)) {
						set_ending("Encoder worker failed to write JPEG");
					}
					frame = frame->next;
				}
			}
			buffer_clear(&frames);
	}
	buffer_destroy(&frames);
}

static void log_metrics()
{
	static offset_t prev_frames;
	pthread_mutex_lock(&mutex);
	struct decoder_errors errors = decoder_errors;
	offset_t frames = frame_counter;
	pthread_mutex_unlock(&mutex);
	float fps = (frames - prev_frames) * 1.0f / metrics_period_s;
	prev_frames = frames;
	log("Frames emitted so far: %lu @ %.1fHz", frames, fps);
	if (errors.no_signal_or_overrun) {
		log("Decoder errors since start: no_signal_or_overrun = %lu", errors.no_signal_or_overrun);
	}
	if (errors.unrecognised_pulse_type) {
		log("Decoder errors since start: unrecognised_pulse_type = %lu", errors.unrecognised_pulse_type);
	}
	if (errors.long_sync_pattern) {
		log("Decoder errors since start: long_sync_pattern = %lu", errors.long_sync_pattern);
	}
	if (errors.unrecognised_sync_pattern) {
		log("Decoder errors since start: unrecognised_sync_pattern = %lu", errors.unrecognised_sync_pattern);
	}
}

static void start_pipeline()
{
	pthread_create(&worker_receiver, NULL, start_worker, run_receiver);
	pthread_create(&worker_decoder, NULL, start_worker, run_decoder);
	pthread_create(&worker_image_encoder, NULL, start_worker, run_image_encoder);
}

static void stop_pipeline()
{
	set_ending("Pipeline stopping");
	pthread_join(worker_receiver, NULL);
	pthread_join(worker_decoder, NULL);
	pthread_join(worker_image_encoder, NULL);
}

static void main_loop()
{
	/* Exit event */
	ending = eventfd(0, EFD_NONBLOCK);
	/* Metrics update timer */
	int timers = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
	assert_equal(true, timers >= 0);
	struct itimerspec timerspec = {
		.it_interval = {
			.tv_sec = metrics_period_s,
			.tv_nsec = 0,
		}
	};
	assert_equal(0, clock_gettime(CLOCK_MONOTONIC, &timerspec.it_value));
	assert_equal(0, timerfd_settime(timers, TFD_TIMER_ABSTIME, &timerspec, 0));
	/* Signal handler */
	sigset_t signal_mask;
	sigemptyset(&signal_mask);
	sigaddset(&signal_mask, SIGTERM);
	sigaddset(&signal_mask, SIGINT);
	sigaddset(&signal_mask, SIGQUIT);
	sigaddset(&signal_mask, SIGPIPE);
	sigaddset(&signal_mask, SIGHUP);
	assert_equal(0, sigprocmask(SIG_BLOCK, &signal_mask, NULL));
	int signals = signalfd(-1, &signal_mask, SFD_NONBLOCK);
	assert_equal(true, signals >= 0);
	/* Main event-loop loop (separate to the actual work, which is threaded) */
	struct pollfd polls[3] = {
		{ .fd = ending, .events = POLLIN, },
		{ .fd = timers, .events = POLLIN, },
		{ .fd = signals, .events = POLLIN, },
	};
	start_pipeline();
	while (true) {
		int poll_result = poll(polls, sizeof(polls) / sizeof(polls[0]), -1);
		assert_equal(true, poll_result >= 0);
		/* Handle exit */
		if (polls[0].revents & POLLIN) {
			log("Exit event received by main thread");
			break;
		}
		/* Handle timer */
		if (polls[1].revents & POLLIN) {
			uint64_t expirations;
			assert_equal(sizeof(expirations), read(timers, &expirations, sizeof(expirations)));
			log_metrics();
		}
		/* Handle signals */
		if (polls[2].revents & POLLIN) {
			struct signalfd_siginfo siginfo;
			assert_equal(sizeof(siginfo), read(signals, &siginfo, sizeof(siginfo)));
			set_ending(strsignal(siginfo.ssi_signo));
		}
	}
	stop_pipeline();
	/* Signal handler */
	close(signals);
	/* Timer */
	close(timers);
	/* Exit event */
	close(ending);
}

int main(int argc, char *argv[])
{
	int range_mv = 1500;
	log("Initialising scope");
	log(" Target period = %uns (%.3fMHz)", (unsigned) target_sample_period_ns, 1e3 / target_sample_period_ns);
	log(" Target range = %.3fV", range_mv / 1000.0f);
	uint32_t actual_sample_period_ns = target_sample_period_ns;
	scope_init(&scope, &actual_sample_period_ns, chunk_length, range_mv);
	log(" Actual period = %uns (%.3fMHz)", (unsigned) actual_sample_period_ns, 1e3 / actual_sample_period_ns);
	log(" Actual range = %.3fV", scope.range_max_mv / 1000.0f);
	decoder_config.sample_period_ns = actual_sample_period_ns;
	/* Inter-thread queues */
	buffer_init(&analog_signal);
	buffer_init(&image_frames);
	/* Synchronisation primitives */
	pthread_mutex_init(&mutex, NULL);
	pthread_cond_init(&analog_signal_cond, NULL);
	pthread_cond_init(&image_frames_cond, NULL);
	/* Real-time scheduling */
	struct sched_param sched_param;
	sched_param.sched_priority = sched_get_priority_max(SCHED_RR);
	sched_setscheduler(0, SCHED_RR, &sched_param);
	/* Main loop */
	main_loop();
	/* Synchronisation primitives */
	pthread_cond_destroy(&analog_signal_cond);
	pthread_cond_destroy(&image_frames_cond);
	pthread_mutex_destroy(&mutex);
	/* Inter-thread queues */
	buffer_destroy(&image_frames);
	buffer_destroy(&analog_signal);
	log("Shutting down scope");
	scope_destroy(&scope);
}
