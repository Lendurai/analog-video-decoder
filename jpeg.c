#include "jpeg.h"
#include <setjmp.h>
#include <jpeglib.h>
#include <jerror.h>

struct jpeg_error_handler
{
	struct jpeg_error_mgr err;
	jmp_buf error_handler;
	char error_text[JMSG_LENGTH_MAX];
};

static void jpeg_on_error_exit(struct jpeg_common_struct *jpeg)
{
	struct jpeg_error_handler *eh = (void *) jpeg->err;
    eh->err.format_message(jpeg, eh->error_text);
    longjmp(eh->error_handler, 1);
}

bool jpeg_write_image(FILE *sink, unsigned width, unsigned height, bool rgb, void *data, unsigned quality)
{
	bool result = false;
	struct jpeg_error_handler eh;
	struct jpeg_compress_struct info;
	info.err = jpeg_std_error(&eh.err);
	eh.err.error_exit = jpeg_on_error_exit;
	if (setjmp(eh.error_handler)) {
		goto done;
	}
	jpeg_create_compress(&info);
	jpeg_stdio_dest(&info, sink);
	info.image_width = width;
	info.image_height = height;
	info.input_components = rgb ? 3 : 1;
	info.in_color_space = rgb ? JCS_RGB : JCS_GRAYSCALE;
	jpeg_set_defaults(&info);
	jpeg_set_quality(&info, quality, TRUE);
	jpeg_start_compress(&info, TRUE);
	uint8_t *scanline = data;
	for (unsigned row = 0; row < height; row++) {
		jpeg_write_scanlines(&info, &scanline, 1);
		scanline += (rgb ? 3 : 1) * width;
	}
	jpeg_finish_compress(&info);
	result = true;
done:
	jpeg_destroy_compress(&info);
	return result;
}
