#define _POSIX_C_SOURCE 200809L

#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int
rawclip_set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

int
rawclip_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

uint64_t
rawclip_now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return ((uint64_t)ts.tv_sec * 1000U) + (uint64_t)(ts.tv_nsec / 1000000L);
}

int
rawclip_parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long value;

    if (s == NULL || *s == '\0' || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    value = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        errno = EINVAL;
        return -1;
    }
    *out = (uint64_t)value;
    return 0;
}

char *
rawclip_xstrndup(const char *s, size_t n)
{
    char *p = malloc(n + 1U);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

int
rawclip_write_all(int fd, const void *buf, size_t len)
{
    const unsigned char *p = buf;
    while (len > 0U) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

int
rawclip_read_all(int fd, void *buf, size_t len)
{
    unsigned char *p = buf;
    while (len > 0U) {
        ssize_t n = read(fd, p, len);
        if (n == 0) {
            errno = ECONNRESET;
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}
