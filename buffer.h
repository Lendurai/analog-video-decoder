#pragma once
#include "stdinc.h"

typedef uint64_t offset_t;
typedef int_fast32_t sample_t;

struct buffer_chunk
{
	struct buffer_chunk *prev;
	struct buffer_chunk *next;
	offset_t offset;
	size_t length;
	sample_t data[];
};

struct buffer
{
	struct buffer_chunk *head;
	struct buffer_chunk *tail;
	size_t chunks;
	size_t samples;
};

void buffer_init(struct buffer *self);
void buffer_destroy(struct buffer *self);
void buffer_clear(struct buffer *self);
struct buffer_chunk *buffer_append(struct buffer *self, size_t length);
void buffer_delete_before(struct buffer *self, struct buffer_chunk *chunk);
void buffer_delete_before_and_including(struct buffer *self, struct buffer_chunk *chunk);
void buffer_concatenate(struct buffer *self, struct buffer *after);
bool buffer_is_empty(struct buffer *self);
