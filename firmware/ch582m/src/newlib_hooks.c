/* Minimal single-threaded newlib integration for the WCH UART firmware. */

#include <errno.h>
#include <reent.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}

int _close(int file)
{
    (void)file;
    errno = ENOSYS;
    return -1;
}

int _fstat(int file, struct stat *status)
{
    (void)file;
    status->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file)
{
    (void)file;
    return 1;
}

off_t _lseek(int file, off_t offset, int whence)
{
    (void)file;
    (void)offset;
    (void)whence;
    return 0;
}

ssize_t _read(int file, void *buffer, size_t length)
{
    (void)file;
    (void)buffer;
    (void)length;
    errno = EAGAIN;
    return -1;
}
