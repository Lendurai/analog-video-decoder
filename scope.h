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
	pthread_mutex_t mutex;
	offset_t samples_read;
};

void scope_init(struct scope *self, uint32_t *period_ns, size_t chunk_length, sample_t range_max_mv);
bool scope_capture(struct scope *self);
void scope_destroy(struct scope *self);
bool scope_take_data(struct scope *self, struct buffer *out);
