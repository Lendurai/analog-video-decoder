#include "buffer.h"

void buffer_init(struct buffer *self)
{
	self->head = NULL;
	self->tail = NULL;
	self->chunks = 0;
	self->samples = 0;
}

void buffer_destroy(struct buffer *self)
{
	buffer_clear(self);
}

void buffer_clear(struct buffer *self)
{
	struct buffer_chunk *it = self->tail;
	while (it) {
		struct buffer_chunk *victim = it;
		it = it->next;
		free(victim);
	}
	self->head = NULL;
	self->tail = NULL;
	self->chunks = 0;
	self->samples = 0;
}

struct buffer_chunk *buffer_append(struct buffer *self, size_t length)
{
	struct buffer_chunk *chunk = malloc(sizeof(*chunk) + length * sizeof(*chunk->data));
	chunk->length = length;
	chunk->prev = self->head;
	chunk->next = NULL;
	if (chunk->prev) {
		chunk->prev->next = chunk;
	} else {
		self->tail = chunk;
	}
	self->head = chunk;
	self->chunks++;
	self->samples += length;
	return chunk;
}

void buffer_delete_before(struct buffer *self, struct buffer_chunk *chunk)
{
	if (!chunk) {
		return;
	}
	buffer_delete_before_and_including(self, chunk->prev);
}

void buffer_delete_before_and_including(struct buffer *self, struct buffer_chunk *chunk)
{
	if (!chunk) {
		return;
	}
	struct buffer_chunk *next = chunk->next;
	while (chunk) {
		struct buffer_chunk *victim = chunk;
		chunk = chunk->prev;
		self->chunks--;
		self->samples -= victim->length;
		free(victim);
	}
	self->tail = next;
	if (next) {
		next->prev = NULL;
	} else {
		self->head = NULL;
	}
}

void buffer_concatenate(struct buffer *self, struct buffer *after)
{
	if (buffer_is_empty(after)) {
		return;
	}
	if (buffer_is_empty(self)) {
		self->head = after->head;
		self->tail = after->tail;
	} else {
		self->head->next = after->tail;
		after->tail->prev = self->head;
		self->head = after->head;
	}
	self->chunks += after->chunks;
	self->samples += after->samples;
	after->head = NULL;
	after->tail = NULL;
	after->chunks = 0;
	after->samples = 0;
}

bool buffer_is_empty(struct buffer *self)
{
	return self->head == NULL;
}
