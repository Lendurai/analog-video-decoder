#include "pattern_buffer.h"

void pattern_buffer_init(struct pattern_buffer *self, uint32_t capacity)
{
	self->capacity = capacity;
	self->overflowed = false;
	self->buffer = malloc(capacity);
}

bool pattern_buffer_next(struct pattern_buffer *self, char value)
{
	char *buffer = self->buffer;
	uint32_t capacity = self->capacity;
	bool overflowed = buffer[capacity - 1] != 0;
	memmove(&buffer[1], &buffer[0], capacity - 1);
	buffer[0] = value;
	return !overflowed;
}

void pattern_buffer_clear(struct pattern_buffer *self)
{
	memset(self->buffer, 0, self->capacity);
}

bool pattern_buffer_match(struct pattern_buffer *self, const char *reverse_pattern)
{
	return strncmp(self->buffer, reverse_pattern, self->capacity) == 0;
}

void pattern_buffer_destroy(struct pattern_buffer *self)
{
	free(self->buffer);
}
