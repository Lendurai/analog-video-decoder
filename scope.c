#include "stdinc.h"
#include "errors.h"
#include "scope.h"

#include <unistd.h>

#include "include/ps2000a/ps2000aApi.h"

static const unsigned pico_range_mv[PS2000A_MAX_RANGES] = {
	10,
	20,
	50,
	100,
	200,
	500,
	1000,
	2000,
	5000,
	10000,
	20000,
	50000,
};

static bool get_range_id(sample_t range_max_mv, PS2000A_RANGE *range_id)
{
	for (PS2000A_RANGE id = 0; id < PS2000A_MAX_RANGES; ++id) {
		if (pico_range_mv[id] >= range_max_mv) {
			*range_id = id;
			return true;
		}
	}
	return false;
}

static void scope_on_data(
	int16_t handle,
	int32_t sample_count,
	uint32_t start_index,
	int16_t overflow,
	uint32_t triggered_at,
	int16_t triggered,
	int16_t auto_stop,
	void *pself
) {
	struct scope *self = pself;
	if (sample_count == 0) {
		return;
	}
	struct adc_buffer *buffer = malloc(sizeof(*buffer) + sample_count * sizeof(*buffer->data));
	buffer->offset = self->samples_read;
	buffer->length = sample_count;
	memcpy(buffer->data, &self->receive_buffer[start_index], sample_count * sizeof(*buffer->data));
	self->samples_read += sample_count;
	if (self->raw_buffer) {
		self->raw_buffer->next = buffer;
	}
	buffer->prev = self->raw_buffer;
	buffer->next = NULL;
	self->raw_buffer = buffer;
	self->overflow = self->overflow || overflow;
}

static inline sample_t scope_convert_adc_sample_to_mv(struct scope *self, adc_sample_t adc_value)
{
	return ((int32_t) adc_value) * self->range_max_mv / self->adc_max_value;
}

static void scope_log_unit_info(struct scope *self)
{
	short handle = self->handle;
	int8_t info[100];
	int16_t tmp;
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_DRIVER_VERSION));
	log("Driver version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_USB_VERSION));
	log("USB version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_HARDWARE_VERSION));
	log("Hardware version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_VARIANT_INFO));
	log("Device variant: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_BATCH_AND_SERIAL));
	log("Device batch and serial: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_CAL_DATE));
	log("Device calibration date: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_KERNEL_VERSION));
	log("Kernel driver version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_DIGITAL_HARDWARE_VERSION));
	log("Device digital hardware version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_ANALOGUE_HARDWARE_VERSION));
	log("Device analog hardware version: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_FIRMWARE_VERSION_1));
	log("Device firmware version 1: %s", info);
	assert_equal(PICO_OK, ps2000aGetUnitInfo(handle, &info[0], sizeof(info), &tmp, PICO_FIRMWARE_VERSION_2));
	log("Device firmware version 2: %s", info);
}

/******************************************************************************/

void scope_init(struct scope *self, const struct scope_config *requested_config, struct scope_config *actual_config)
{
	/* Voltage range */
	log("Requesting range: %.3fV", requested_config->range_max_mv / 1000.0f);
	PS2000A_RANGE range_id;
	assert_equal(
		true,
		get_range_id(requested_config->range_max_mv, &range_id)
	);
	uint32_t range_mv = pico_range_mv[range_id];
	self->range_max_mv = range_mv;
	log("Using range: %.3fV", range_mv / 1000.0f);
	/* Device */
	log("Connecting to scope");
	short handle;
	assert_equal(
		PICO_OK,
		ps2000aOpenUnit(&handle, NULL)
	);
	assert_not_equal(-1, handle, "Failed to open oscilloscope");
	assert_not_equal(0, handle, "No oscilloscope found");
	self->handle = handle;
	/* Device info */
	scope_log_unit_info(self);
	/* Configure channels */
	assert_equal(
		PICO_OK,
		ps2000aSetChannel(handle, PS2000A_CHANNEL_A, true, PS2000A_DC, range_id, 0)
	);
	assert_equal(
		PICO_OK,
		ps2000aSetChannel(handle, PS2000A_CHANNEL_B, false, PS2000A_DC, PS2000A_50V, 0)
	);
	assert_equal(
		PICO_OK,
		ps2000aSetSimpleTrigger(handle, false, PS2000A_CHANNEL_A, 0, PS2000A_RISING, 0, 0)
	);
	/* Buffers */
	log("Configuring data buffer");
	uint32_t ratio_mode = requested_config->oversample_ratio > 1 ? PS2000A_RATIO_MODE_AVERAGE : PS2000A_RATIO_MODE_NONE;
	size_t receive_buffer_length = requested_config->chunk_max_samples * requested_config->max_chunks_in_queue;
	log("Read chunk size: %uS", requested_config->chunk_max_samples);
	log("Overview buffer capacity: %u reads / %zuS", requested_config->max_chunks_in_queue, receive_buffer_length);
	adc_sample_t *receive_buffer = malloc(sizeof(*receive_buffer) * receive_buffer_length);
	self->receive_buffer = receive_buffer;
	assert_equal(
		PICO_OK,
		ps2000aSetDataBuffer(handle, PS2000A_CHANNEL_A, self->receive_buffer, receive_buffer_length, 0, ratio_mode)
	);
	/* Stream (sample-rate + oversample ratio) */
	log("Configuring stream");
	uint32_t oversample_ratio = requested_config->oversample_ratio;
	log(
		"Requesting oversample ratio %u @ reduced sample-rate %.2fMHz",
		oversample_ratio,
		1e6 / requested_config->user_sample_period_ps
	);
	uint32_t max_oversample_ratio = oversample_ratio;
	log("Receive-buffer size: %.1fMS", receive_buffer_length * oversample_ratio / 1048576.0);
	assert_equal(
		PICO_OK,
		ps2000aGetMaxDownSampleRatio(handle, receive_buffer_length * oversample_ratio, &max_oversample_ratio, ratio_mode, 0)
	);
	if (oversample_ratio > max_oversample_ratio) {
		oversample_ratio = max_oversample_ratio;
	}
	log("Using oversample ratio %u (max: %u)", oversample_ratio, max_oversample_ratio);
	uint32_t device_sample_period_ps = requested_config->user_sample_period_ps / oversample_ratio;
	log(
		"Requesting sample-rate: %.2fMHz / %u = %.2fMHz (%ups x %u)",
		1e6 / device_sample_period_ps, oversample_ratio,
		1e6 / device_sample_period_ps / oversample_ratio,
		device_sample_period_ps, oversample_ratio
	);
	assert_equal(
		PICO_OK,
		ps2000aRunStreaming(
			handle,
			&device_sample_period_ps,
			PS2000A_PS,
			0,
			0,
			false,
			oversample_ratio,
			ratio_mode,
			requested_config->chunk_max_samples
		)
	);
	log(
		"Using sample-rate: %.2fMHz / %u = %.2fMHz (%ups x %u)",
		1e6 / device_sample_period_ps, oversample_ratio,
		1e6 / device_sample_period_ps / oversample_ratio,
		device_sample_period_ps, oversample_ratio
	);
	/* ADC->Voltage conversion */
	log("Determining ADC/voltage conversion");
	assert_equal(
		PICO_OK,
		ps2000aMaximumValue(handle, &self->adc_max_value)
	);
	uint64_t user_sample_period_ps = device_sample_period_ps * oversample_ratio;
	/* Rest */
	self->overflow = false;
	self->poll_interval_us = (uint64_t) requested_config->chunk_max_samples * user_sample_period_ps / 1000000 / 2;
	self->raw_buffer = NULL;
	self->samples_read = 0;
	log("Poll-loop interval: %uus", self->poll_interval_us);
	/* Return adjusted config */
	if (actual_config) {
		actual_config->oversample_ratio = requested_config->oversample_ratio;
		actual_config->chunk_max_samples = requested_config->chunk_max_samples;
		actual_config->max_chunks_in_queue = requested_config->max_chunks_in_queue;
		actual_config->range_max_mv = range_mv;
		actual_config->user_sample_period_ps = user_sample_period_ps;
		actual_config->device_sample_period_ps = device_sample_period_ps;
	}
}

void scope_destroy(struct scope *self)
{
	short handle = self->handle;
	assert_equal(
		PICO_OK,
		ps2000aStop(handle)
	);
	assert_equal(
		PICO_OK,
		ps2000aCloseUnit(handle)
	);
}

void scope_capture(struct scope *self, struct buffer *out, bool *overflow)
{
	/* Wait for callback to provide some data */
	while (self->raw_buffer == NULL) {
		PICO_STATUS status;
		status = ps2000aGetStreamingLatestValues(self->handle, scope_on_data, self);
		if (status != PICO_BUSY) {
			assert_equal(PICO_OK, status);
			if (self->raw_buffer) {
				break;
			}
		}
		usleep(self->poll_interval_us);
	}
	/* Steal current ADC raw-data chunk list */
	struct adc_buffer *raw_buffer;
	raw_buffer = self->raw_buffer;
	self->raw_buffer = NULL;
	/* Overflow flag */
	*overflow = self->overflow;
	self->overflow = false;
	/* Convert ADC values to voltages */
	while (raw_buffer && raw_buffer->prev) {
		raw_buffer = raw_buffer->prev;
	}
	while (raw_buffer) {
		struct buffer_chunk *chunk = buffer_append(out, raw_buffer->length);
		chunk->offset = raw_buffer->offset;
		for (size_t index = 0; index < raw_buffer->length; ++index) {
			chunk->data[index] = scope_convert_adc_sample_to_mv(self, raw_buffer->data[index]);
		}
		struct adc_buffer *victim = raw_buffer;
		raw_buffer = raw_buffer->next;
		free(victim);
	}
}
