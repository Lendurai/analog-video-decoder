#pragma once
#include "stdinc.h"

struct pattern_buffer
{
	uint32_t capacity;
	bool overflowed;
	char *buffer;
};

void pattern_buffer_init(struct pattern_buffer *self, uint32_t capacity);
bool pattern_buffer_next(struct pattern_buffer *self, char value);
void pattern_buffer_clear(struct pattern_buffer *self);
bool pattern_buffer_match(struct pattern_buffer *self, const char *reverse_pattern);
void pattern_buffer_destroy(struct pattern_buffer *self);
