#include "pulse_width.h"

void pulse_analyser_init(struct pulse_analyser *self, uint64_t initial_offset, bool right_aligned)
{
	self->right_aligned = right_aligned;
	self->rise_at = initial_offset;
	self->fall_at = initial_offset;
	self->last_state = !right_aligned;
}

bool pulse_analyser_transition(struct pulse_analyser *self, uint64_t offset, bool state, struct pulse_info *info)
{
	bool actually_transitioned = state != self->last_state;
	bool correct_edge = state != self->right_aligned;
	bool have_all_timings = self->rise_at != self->fall_at;
	bool is_valid = actually_transitioned && correct_edge && have_all_timings;
	if (is_valid) {
		if (state) {
			info->start = self->rise_at;
			info->transition = self->fall_at;
			info->end = offset;
		} else {
			info->start = self->fall_at;
			info->transition = self->rise_at;
			info->end = offset;
		}
		is_valid = info->end > info->transition && info->transition > info->start;
	}
	if (state) {
		self->rise_at = offset;
	} else {
		self->fall_at = offset;
	}
	self->last_state = state;
	return is_valid;
}

void pulse_analyser_reset(struct pulse_analyser *self, uint64_t offset)
{
	self->rise_at = offset;
	self->fall_at = offset;
}

void pulse_analyser_destroy(struct pulse_analyser *self)
{
	(void) self;
}

void pulse_stream_reader_init(struct pulse_stream_reader *self, struct pulse_analyser *pulse_analyser, sample_t threshold, bool initial_state, uint64_t initial_offset)
{
	self->pulse_analyser = pulse_analyser;
	self->threshold = threshold;
	self->previous_state = initial_state;
	pulse_stream_reader_reset(self);
	pulse_stream_reader_bind(self, NULL);
}

void pulse_stream_reader_bind(struct pulse_stream_reader *self, struct buffer_chunk *buffer)
{
	self->buffer = buffer;
	self->next_sample_index = 0;
}

void pulse_stream_reader_reset(struct pulse_stream_reader *self)
{
	self->reset_pending = true;
}

bool pulse_stream_reader_next(struct pulse_stream_reader *self, struct pulse_info *info)
{
	bool result = false;
	struct buffer_chunk *buffer = self->buffer;
	if (!buffer) {
		return false;
	}
	if (self->reset_pending) {
		self->reset_pending = false;
		pulse_analyser_reset(self->pulse_analyser, buffer->offset);
	}
	bool previous_state = self->previous_state;
	size_t next_sample_index = self->next_sample_index;
	sample_t *data = buffer->data;
	sample_t threshold = self->threshold;
	size_t length = buffer->length;
	while (next_sample_index < length) {
		size_t sample_index = next_sample_index++;
		bool state = data[sample_index] >= threshold;
		if (state == previous_state) {
			continue;
		}
		previous_state = state;
		if (pulse_analyser_transition(self->pulse_analyser, buffer->offset + sample_index, state, info)) {
			result = true;
			goto done;
		}
	}
done:
	self->next_sample_index = next_sample_index;
	self->previous_state = previous_state;
	return result;
}

void pulse_stream_reader_destroy(struct pulse_stream_reader *self)
{
	(void) self;
}
