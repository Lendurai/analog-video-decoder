#include "stdinc.h"
#include "errors.h"

void _vlog(const char *file, int line, const char *func, const char *format, va_list ap)
{
	fprintf(stderr, "%20.20s:%5u (%s) :: ", file, line, func);
	vfprintf(stderr, format, ap);
	fprintf(stderr, "\n");
	fflush(stderr);
}

__attribute__((__format__(__printf__, 4, 5)))
void _log(const char *file, int line, const char *func, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_vlog(file, line, func, format, ap);
	va_end(ap);
}

__attribute__((__format__(__printf__, 4, 5)))
void _fatal_error(const char *file, int line, const char *func, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	_vlog(file, line, func, format, ap);
	va_end(ap);
	fflush(stderr);
	exit(1);
}

void _assert_equal(int expect, int actual, const char *file, int line, const char *func, char *expr)
{
	if (actual != expect) {
		_fatal_error(file, line, func, "ERROR: expected %d, got %d :: %s\n", expect, actual, expr);
	}
}

void _assert_not_equal(int not_expect, int actual, const char *file, int line, const char *func, char *expr)
{
	if (actual == not_expect) {
		_fatal_error(file, line, func, "ERROR: expected anything except %d, got %d :: %s\n", not_expect, actual, expr);
	}
}
