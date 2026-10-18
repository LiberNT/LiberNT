#define HAVE_ERRNO_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_UTIME_H 1
#define STDC_HEADERS 1
#define PACKAGE "wimlib"
#define PACKAGE_NAME "wimlib"
#define PACKAGE_TARNAME "wimlib"
#define PACKAGE_VERSION "1.14.5"
#define PACKAGE_STRING "wimlib 1.14.5"
#define PACKAGE_BUGREPORT "https://wimlib.net/forums/"
#define PACKAGE_URL ""
#define VERSION "1.14.5"


#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

static __inline int wimlib_ftruncate64(int fd, __int64 size)
{
    return _chsize_s(fd, size) ? -1 : 0;
}

static __inline wchar_t *wmempcpy(wchar_t *dst, const wchar_t *src, size_t n)
{
    return (wchar_t *)memcpy(dst, src, n * sizeof(wchar_t)) + n;
}

static __inline struct tm *gmtime_r(const __time64_t *timer, struct tm *result)
{
    return _gmtime64_s(result, timer) ? NULL : result;
}

#define off_t __int64
#define lseek _lseeki64
#define stat _stat64
#define fstat _fstat64
#define wstat _wstat64
#define ftruncate wimlib_ftruncate64
#define snwprintf _snwprintf
#ifdef _USE_32BIT_TIME_T
#define time_t __time64_t
#define time _time64
#define gmtime _gmtime64
#define localtime _localtime64
#define mktime _mktime64
#endif
