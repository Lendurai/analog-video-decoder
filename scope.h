#pragma once
#include "stdinc.h"

#include "buffer.h"

typedef int16_t adc_sample_t;

struct adc_buffer
{
	offset_t offset;
	size_t length;
	struct adc_buffer *prev;
	struct adc_buffer *next;
	adc_sample_t data[];
};

struct scope
{
	short handle;
	sample_t range_max_mv;
	adc_sample_t adc_max_value;
	adc_sample_t *receive_buffer;
	struct adc_buffer *raw_buffer;
	offset_t samples_read;
	bool overflow;
	uint32_t poll_interval_us;
};

struct scope_config
{
	/* Input */
	uint32_t oversample_ratio;
	uint32_t chunk_max_samples;
	uint32_t max_chunks_in_queue;
	/* Input/Output */
	sample_t range_max_mv;
	uint64_t user_sample_period_ps;
	/* Output */
	uint64_t device_sample_period_ps;
};

void scope_init(struct scope *self, const struct scope_config *requested_config, struct scope_config *actual_config);
void scope_capture(struct scope *self, struct buffer *out, bool *overflow);
void scope_destroy(struct scope *self);
