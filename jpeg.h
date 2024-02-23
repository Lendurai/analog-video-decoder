#pragma once
#include "stdinc.h"

bool jpeg_write_image(FILE *sink, unsigned width, unsigned height, bool rgb, void *data, unsigned quality);
