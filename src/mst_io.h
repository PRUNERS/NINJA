#ifndef _MST_IO_H_
#define _MST_IO_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int mst_open(const char* file, int flags, mode_t  mode);
int mst_close(const char* file, int fd);
ssize_t mst_write(const char* file, int fd, const void* buf, size_t size);
ssize_t mst_read(const char* file, int fd, void* buf, size_t size);

#endif
