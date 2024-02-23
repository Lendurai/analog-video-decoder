#pragma once
#include "stdinc.h"
#include "buffer.h"
#include "pulse_width.h"

struct decoder_config
{
	uint32_t sample_period_ns;
	bool interlaced;
	uint32_t frame_width;
	uint32_t frame_height;
	sample_t sync_threshold;
	sample_t black_level;
	sample_t white_level;
	size_t max_backlog_samples;
	uint32_t sync_duration_ns;
	uint32_t line_duration_ns;
	uint32_t equaliser_low_ns;
	uint32_t vertical_sync_low_ns;
	uint32_t horizontal_sync_low_ns;
	uint32_t front_porch_ns;
	uint32_t back_porch_ns;
	uint32_t tolerance_ns;
};

enum
{
	pulse_history_length = 15,
};

struct decoder_errors
{
	uint64_t no_signal_or_overrun;
	uint64_t unrecognised_pulse_type;
	uint64_t long_sync_pattern;
	uint64_t unrecognised_sync_pattern;
};

struct decoder
{
	/* Configuration */
	struct decoder_config config;
	/* Sample buffer */
	struct buffer buffer;
	struct buffer_chunk *current;
	offset_t next_chunk_expected_offset;
	/* Pulse decoder state */
	struct pulse_analyser pulse_analyser;
	struct pulse_stream_reader pulse_stream_reader;
	/* PAL decoder state */
	char pulse_pattern[pulse_history_length];
	/* Image buffer */
	uint32_t next_line;
	uint8_t *frame;
	bool frame_ready;
	/* Error counters */
	struct decoder_errors errors;
};

void decoder_init(struct decoder *self, const struct decoder_config *config);
void decoder_bind_and_steal(struct decoder *self, struct buffer *new_data);
bool decoder_read_frame(struct decoder *self);
void decoder_reset_error_counters(struct decoder *self, struct decoder_errors *out);
void decoder_destroy(struct decoder *self);
