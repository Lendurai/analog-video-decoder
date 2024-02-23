#pragma once

__attribute__((__format__(__printf__, 4, 5)))
void _fatal_error(const char *file, int line, const char *func, const char *format, ...);
#define fatal_error(format, ...) \
	_fatal_error(__FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

void _assert_equal(int expect, int actual, const char *file, int line, const char *func, char *expr);
#define assert_equal(expect, expr) \
	_assert_equal(expect, (expr), __FILE__, __LINE__, __func__, #expr)

void _assert_not_equal(int not_expect, int actual, const char *file, int line, const char *func, char *expr);
#define assert_not_equal(not_expect, expr, msg) \
	_assert_not_equal(not_expect, (expr), __FILE__, __LINE__, __func__, msg)

__attribute__((__format__(__printf__, 4, 5)))
void _log(const char *file, int line, const char *func, const char *format, ...);
#define log(format, ...) \
	_log(__FILE__, __LINE__, __func__, format, ##__VA_ARGS__)
