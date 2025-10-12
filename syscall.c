#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

typedef int (*open_t)(const char *pathname, int flags);
open_t open_f = NULL;

typedef ssize_t (*read_t)(int fd, void* buf, size_t count);
read_t read_f = NULL;

typedef ssize_t (*write_t)(int fd, const void* buf, size_t count);
write_t write_f = NULL;

typedef int (*close_t)(int fd);
close_t close_f = NULL;

typedef off_t (*lseek_t)(int fd, off_t offset, int whence);
lseek_t lseek_f = NULL;

typedef int (*create_t)(const char *pathname, mode_t mode);
create_t create_f = NULL;

int open(const char *pathname, int flags) {
    if (!open_f) {
        open_f = dlsym(RTLD_NEXT, "open");
    }
    return open_f(pathname, flags);
}

int create(const char *pathname, mode_t mode) {
    if (!create_f) {
        create_f = dlsym(RTLD_NEXT, "creat");
    }
    return create_f(pathname, mode);
}

ssize_t read(int fd, void* buf, size_t count) {
    if (!read_f) {
        read_f = dlsym(RTLD_NEXT, "read");
    }
    return read_f(fd, buf, count);
}

ssize_t write(int fd, const void* buf, size_t count) {
    if (!write_f) {
        write_f = dlsym(RTLD_NEXT, "write");
    }
    return write_f(fd, buf, count);
}

int close(int fd) {
    if (!close_f) {
        close_f = dlsym(RTLD_NEXT, "close");
    }
    return close_f(fd);
}
