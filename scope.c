#include "stdinc.h"
#include "errors.h"
#include "scope.h"

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
	assert_equal(0, overflow);
	if (sample_count == 0) {
		return;
	}
	struct adc_buffer *buffer = malloc(sizeof(*buffer) + sample_count * sizeof(*buffer->data));
	buffer->offset = self->samples_read;
	buffer->length = sample_count;
	memcpy(buffer->data, &self->receive_buffer[start_index], sample_count * sizeof(*buffer->data));
	self->samples_read += sample_count;
	pthread_mutex_lock(&self->mutex);
	if (self->raw_buffer) {
		self->raw_buffer->next = buffer;
	}
	buffer->prev = self->raw_buffer;
	buffer->next = NULL;
	self->raw_buffer = buffer;
	pthread_mutex_unlock(&self->mutex);
}

void scope_init(struct scope *self, uint32_t *period_ns, size_t chunk_length, sample_t range_max_mv)
{
	PS2000A_RANGE range_id;
	assert_equal(
		true,
		get_range_id(range_max_mv, &range_id)
	);
	size_t receive_buffer_length = chunk_length * 10;
	adc_sample_t *receive_buffer = malloc(sizeof(*receive_buffer) * receive_buffer_length);
	self->receive_buffer = receive_buffer;
	self->range_max_mv = pico_range_mv[range_id];
	short handle;
	assert_equal(
		PICO_OK,
		ps2000aOpenUnit(&handle, NULL)
	);
	assert_not_equal(-1, handle, "Failed to open oscilloscope");
	assert_not_equal(0, handle, "No oscilloscope found");
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
	assert_equal(
		PICO_OK,
		ps2000aSetDataBuffer(handle, PS2000A_CHANNEL_A, self->receive_buffer, receive_buffer_length, 0, PS2000A_RATIO_MODE_NONE)
	);
	assert_equal(
		PICO_OK,
		ps2000aRunStreaming(handle, period_ns, PS2000A_NS, 0, 0, false, 1, PS2000A_RATIO_MODE_NONE, chunk_length)
	);
	self->handle = handle;
	self->raw_buffer = NULL;
	self->samples_read = 0;
	assert_equal(
		PICO_OK,
		ps2000aMaximumValue(handle, &self->adc_max_value)
	);
	pthread_mutex_init(&self->mutex, NULL);
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
	pthread_mutex_destroy(&self->mutex);
}

bool scope_capture(struct scope *self)
{
	PICO_STATUS status = ps2000aGetStreamingLatestValues(self->handle, scope_on_data, self);
	if (status == PICO_BUSY) {
		return false;
	}
	assert_equal(
		PICO_OK,
		status
	);
	return true;
}

static inline sample_t scope_convert_adc_sample_to_mv(struct scope *self, adc_sample_t adc_value)
{
	return ((int32_t) adc_value) * self->range_max_mv / self->adc_max_value;
}

bool scope_take_data(struct scope *self, struct buffer *out)
{
	/* Steal current ADC-data chain */
	struct adc_buffer *raw_buffer;
	pthread_mutex_lock(&self->mutex);
	raw_buffer = self->raw_buffer;
	self->raw_buffer = NULL;
	pthread_mutex_unlock(&self->mutex);
	if (raw_buffer == NULL) {
		return false;
	}
	while (raw_buffer->prev) {
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
	return true;
}
