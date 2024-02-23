#pragma once
#include "stdinc.h"
#include "buffer.h"

static const bool pulse_left_aligned = false;
static const bool pulse_right_aligned = true;

struct pulse_info
{
	uint64_t start;
	uint64_t transition;
	uint64_t end;
};

struct pulse_analyser
{
	bool right_aligned;
	uint64_t rise_at;
	uint64_t fall_at;
	bool last_state;
};

void pulse_analyser_init(struct pulse_analyser *self, uint64_t initial_offset, bool right_aligned);
bool pulse_analyser_transition(struct pulse_analyser *self, uint64_t offset, bool state, struct pulse_info *info);
void pulse_analyser_reset(struct pulse_analyser *self, uint64_t offset);
void pulse_analyser_destroy(struct pulse_analyser *self);

struct pulse_stream_reader
{
	struct pulse_analyser *pulse_analyser;
	sample_t threshold;
	bool previous_state;
	struct buffer_chunk *buffer;
	size_t next_sample_index;
	bool reset_pending;
};

void pulse_stream_reader_init(struct pulse_stream_reader *self, struct pulse_analyser *pulse_analyser, sample_t threshold, bool initial_state, uint64_t initial_offset);
void pulse_stream_reader_bind(struct pulse_stream_reader *self, struct buffer_chunk *buffer);
bool pulse_stream_reader_next(struct pulse_stream_reader *self, struct pulse_info *info);
void pulse_stream_reader_reset(struct pulse_stream_reader *self);
void pulse_stream_reader_destroy(struct pulse_stream_reader *self);
