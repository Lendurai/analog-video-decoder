#include "decoder.h"
#include "buffer.h"
#include "errors.h"
#include "pattern_buffer.h"
#include "pulse_width.h"

enum
{
	longest_sync_pattern_length = 15,
};

enum pulse_type
{
	pulse_type_none = 0,
	pulse_type_equaliser = 'e',
	pulse_type_vertical = 'v',
	pulse_type_horizontal = 'h',
	pulse_type_field = 'f',
};

enum pattern_type
{
	pattern_type_none,
	pattern_type_next_frame,
	pattern_type_next_field,
};

/* REVERSED, i.e. first char is last pulse of pattern */
static const char pattern_next_frame[longest_sync_pattern_length] = "eeeeevvvvveeeee";
static const char pattern_next_field[longest_sync_pattern_length] = "feeeevvvvveeeee";

static uint8_t *decoder_next_line(struct decoder *self)
{
	uint32_t this_line = self->next_line;
	if (this_line >= self->config.frame_height) {
		return NULL;
	}
	self->next_line += self->config.interlaced ? 2 : 1;
	return &self->frame[this_line * self->config.frame_width];
}

static void decoder_select_field(struct decoder *self, int field)
{
	if (self->config.interlaced && field == 1) {
		self->next_line = 1;
	} else {
		self->next_line = 0;
	}
}

static void decoder_reset_frame(struct decoder *self)
{
	memset(self->frame, 0, self->config.frame_width * self->config.frame_height);
	self->next_line = 0;
	self->frame_ready = false;
}

static struct buffer_chunk *decoder_seek(struct decoder *self, struct buffer_chunk *initial, offset_t offset)
{
	struct buffer_chunk *chunk = initial;
	if (chunk == NULL) {
		return NULL;
	}
	while (chunk->next && chunk->offset + chunk->length <= offset) {
		chunk = chunk->next;
	}
	while (chunk->prev && chunk->offset > offset) {
		chunk = chunk->prev;
	}
	assert_equal(true, offset >= chunk->offset);
	assert_equal(true, offset < chunk->offset + chunk->length);
	return chunk;
}

static inline uint8_t decoder_convert_brightness(struct decoder *self, sample_t value)
{
	sample_t black = self->config.black_level;
	sample_t white = self->config.white_level;
	if (value < black) {
		return 0;
	} else if (value > white) {
		return 255;
	} else {
		return 255 * (value - black) / (white - black);
	}
}

static void decoder_process_line(struct decoder *self, offset_t high_begin, offset_t high_end)
{
	uint8_t *line = decoder_next_line(self);
	if (line == NULL) {
		return;
	}
	size_t width = self->config.frame_width;
	offset_t back_porch = (uint64_t) self->config.back_porch_ns * 1000 / self->config.sample_period_ps;
	offset_t front_porch = (uint64_t) self->config.front_porch_ns * 1000 / self->config.sample_period_ps;
	offset_t data_begin = high_begin + back_porch;
	offset_t data_end = high_end - front_porch;
	offset_t data_duration = data_end - data_begin;
	struct buffer_chunk *chunk = self->current;
	for (uint32_t col = 0; col < width; col++) {
		offset_t offset = data_begin + (data_duration * col / width);
		chunk = decoder_seek(self, chunk, offset);
		if (!chunk) {
			return;
		}
		line[col] = decoder_convert_brightness(self, chunk->data[offset - chunk->offset]);
	}
}

static enum pattern_type decoder_get_sync_pattern(struct decoder *self)
{
	struct pattern_buffer *pattern_buffer = &self->pattern_buffer;
	if (pattern_buffer_match(pattern_buffer, pattern_next_frame)) {
		return pattern_type_next_frame;
	} else if (pattern_buffer_match(pattern_buffer, pattern_next_field)) {
		return pattern_type_next_field;
	} else {
		return pattern_type_none;
	}
}

static void decoder_process_pulse_pattern(struct decoder *self)
{
	enum pattern_type type = decoder_get_sync_pattern(self);
	if (type == pattern_type_none) {
		return;
	}
	if (type == pattern_type_next_frame) {
		self->frame_ready = true;
		decoder_select_field(self, 0);
	} else if (type == pattern_type_next_field) {
		decoder_select_field(self, 1);
	}
	pattern_buffer_clear(&self->pattern_buffer);
}

static bool is_similar(uint32_t measurement, uint32_t reference, uint32_t tolerance)
{
	int32_t difference = measurement;
	difference -= reference;
	return abs(difference) <= tolerance;
}

enum pulse_type decoder_characterise_pulse(struct decoder *self, uint32_t duration_ns, uint32_t high_ns)
{
	struct decoder_config *config = &self->config;
	uint32_t low_ns = duration_ns - high_ns;
	uint32_t line_duration_ns = self->config.line_duration_ns;
	uint32_t sync_duration_ns = self->config.sync_duration_ns;
	uint32_t horizontal_sync_low_ns = self->config.horizontal_sync_low_ns;
	uint32_t equaliser_low_ns = self->config.equaliser_low_ns;
	uint32_t vertical_sync_low_ns = self->config.vertical_sync_low_ns;
	uint32_t tolerance_ns = self->config.tolerance_ns;
	if (is_similar(duration_ns, line_duration_ns, tolerance_ns) && is_similar(low_ns, horizontal_sync_low_ns, tolerance_ns)) {
		return pulse_type_horizontal;
	}
	if (is_similar(duration_ns, line_duration_ns, tolerance_ns) && is_similar(low_ns, equaliser_low_ns, tolerance_ns)) {
		return pulse_type_field;
	}
	if (is_similar(duration_ns, sync_duration_ns, tolerance_ns) && is_similar(low_ns, horizontal_sync_low_ns, tolerance_ns)) {
		return pulse_type_field;
	}
	if (is_similar(duration_ns, sync_duration_ns, tolerance_ns) && is_similar(low_ns, vertical_sync_low_ns, tolerance_ns)) {
		return pulse_type_vertical;
	}
	if (is_similar(duration_ns, sync_duration_ns, tolerance_ns) && is_similar(low_ns, equaliser_low_ns, tolerance_ns)) {
		return pulse_type_equaliser;
	}
	return pulse_type_none;
}

static void decoder_debug_log_pulse(enum pulse_type type, uint32_t pulse_ns, uint32_t pulse_high_ns, bool force)
{
	static enum pulse_type prev_type;
	static uint32_t prev_count;
	static uint32_t prev_ns;
	static uint32_t prev_high_ns;
	if (type == prev_type) {
		prev_count++;
	}
	if (type != prev_type || force) {
		const char *description =
			prev_type == pulse_type_horizontal ? "horz" :
			prev_type == pulse_type_equaliser ? "eq" :
			prev_type == pulse_type_vertical ? "vert" :
			prev_type == pulse_type_field ? "field" :
			"unknown";
		log(
			"Pulse %4.1f / %4.1f us (%s) x%u",
			prev_high_ns / 1000.0f,
			prev_ns / 1000.0f,
			description,
			prev_count
		);
		prev_count = 1;
		prev_type = type;
		prev_ns = pulse_ns;
		prev_high_ns = pulse_high_ns;
	}
}

static void decoder_process_pulse(struct decoder *self, struct pulse_info *pulse_info)
{
	const uint32_t sample_period_ps = self->config.sample_period_ps;
	/* We trust that the input is valid, such that these won't go negative */
	uint64_t pulse_samples = pulse_info->end - pulse_info->start;
	uint64_t pulse_high_samples = pulse_info->end - pulse_info->transition;
	uint32_t pulse_ns = pulse_samples * sample_period_ps / 1000;
	uint32_t pulse_high_ns = pulse_high_samples * sample_period_ps / 1000;
	enum pulse_type type = decoder_characterise_pulse(self, pulse_ns, pulse_high_ns);
	if (type == pulse_type_horizontal) {
		decoder_process_line(self, pulse_info->transition, pulse_info->end);
	} else if (type != pulse_type_none) {
		if (!pattern_buffer_next(&self->pattern_buffer, type)) {
			self->errors.long_sync_pattern++;
		}
		decoder_process_pulse_pattern(self);
	} else {
		self->errors.unrecognised_pulse_type++;
		pattern_buffer_clear(&self->pattern_buffer);
		/* decoder_debug_log_pulse(type, pulse_ns, pulse_high_ns, true); */
	}
	/* decoder_debug_log_pulse(type, pulse_ns, pulse_high_ns, false); */
}

static void decoder_handle_desync(struct decoder *self)
{
	pulse_stream_reader_reset(&self->pulse_stream_reader);
	pattern_buffer_clear(&self->pattern_buffer);
	decoder_reset_frame(self);
}

static void decoder_bind_chunk(struct decoder *self, struct buffer_chunk *chunk)
{
	self->current = chunk;
	if (!chunk) {
		return;
	}
	if (chunk->offset != self->next_chunk_expected_offset) {
		decoder_handle_desync(self);
	}
	self->next_chunk_expected_offset = chunk->offset + chunk->length;
	pulse_stream_reader_bind(&self->pulse_stream_reader, chunk);
}

static bool decoder_overrun(struct decoder *self)
{
	ssize_t buffered = self->buffer.samples;
	ssize_t limit = self->config.max_backlog_samples;
	return buffered > limit;
}

/******************************************************************************/

void decoder_init(struct decoder *self, const struct decoder_config *config)
{
	log("Initialising decoder @ sample-rate = %.2fMHz", 1e6 / config->sample_period_ps);
	self->config = *config;
	pulse_analyser_init(&self->pulse_analyser, 0, pulse_right_aligned);
	pulse_stream_reader_init(&self->pulse_stream_reader, &self->pulse_analyser, config->sync_threshold, false, 0);
	self->frame = malloc(config->frame_width * config->frame_height);
	self->current = NULL;
	self->next_chunk_expected_offset = 0;
	decoder_reset_frame(self);
	buffer_init(&self->buffer);
	pattern_buffer_init(&self->pattern_buffer, longest_sync_pattern_length);
	decoder_reset_error_counters(self, NULL);
}

void decoder_bind_and_steal(struct decoder *self, struct buffer *new_data)
{
	if (buffer_is_empty(new_data)) {
		return;
	}
	struct buffer_chunk *new_tail = new_data->tail;
	buffer_concatenate(&self->buffer, new_data);
	if (!self->current) {
		decoder_bind_chunk(self, new_tail);
	}
	if (decoder_overrun(self)) {
		self->errors.no_signal_or_overrun++;
		while (decoder_overrun(self)) {
			buffer_delete_before_and_including(&self->buffer, self->buffer.tail);
		}
		decoder_bind_chunk(self, self->buffer.tail);
	}
}

void decoder_destroy(struct decoder *self)
{
	pattern_buffer_destroy(&self->pattern_buffer);
	buffer_destroy(&self->buffer);
	free(self->frame);
	pulse_stream_reader_destroy(&self->pulse_stream_reader);
	pulse_analyser_destroy(&self->pulse_analyser);
}

void decoder_reset_error_counters(struct decoder *self, struct decoder_errors *out)
{
	struct decoder_errors *errors = &self->errors;
	if (out) {
		out->no_signal_or_overrun += errors->no_signal_or_overrun;
		out->unrecognised_pulse_type += errors->unrecognised_pulse_type;
		out->long_sync_pattern += errors->long_sync_pattern;
		out->unrecognised_sync_pattern += errors->unrecognised_sync_pattern;
	}
	errors->no_signal_or_overrun = 0;
	errors->unrecognised_pulse_type = 0;
	errors->long_sync_pattern = 0;
	errors->unrecognised_sync_pattern = 0;
}

bool decoder_read_frame(struct decoder *self)
{
	self->frame_ready = false;
	while (self->current) {
		struct pulse_info pulse_info;
		while (pulse_stream_reader_next(&self->pulse_stream_reader, &pulse_info)) {
			decoder_process_pulse(self, &pulse_info);
			buffer_delete_before(&self->buffer, self->current);
			if (self->frame_ready) {
				goto done;
			}
		}
		decoder_bind_chunk(self, self->current->next);
	}
done:
	return self->frame_ready;
}
