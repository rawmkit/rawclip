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
    uint64_t value = 0U;
    size_t i;

    if (s == NULL || *s == '\0' || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; s[i] != '\0'; i++) {
        unsigned int digit;
        if (s[i] < '0' || s[i] > '9') {
            errno = EINVAL;
            return -1;
        }
        digit = (unsigned int)(s[i] - '0');
        if (value > (UINT64_MAX - digit) / 10U) {
            errno = EINVAL;
            return -1;
        }
        value = value * 10U + digit;
    }
    if (value == 0U) {
        errno = EINVAL;
        return -1;
    }
    *out = value;
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


bool
rawclip_utf8_valid(const unsigned char *s, size_t len)
{
    size_t i = 0U;
    while (i < len) {
        unsigned char c = s[i++];
        uint32_t cp;
        unsigned int need;
        if (c < 0x80U) continue;
        if ((c & 0xe0U) == 0xc0U) { cp = c & 0x1fU; need = 1U; if (cp < 2U) return false; }
        else if ((c & 0xf0U) == 0xe0U) { cp = c & 0x0fU; need = 2U; }
        else if ((c & 0xf8U) == 0xf0U) { cp = c & 0x07U; need = 3U; }
        else return false;
        if (len - i < need) return false;
        while (need-- > 0U) {
            unsigned char d = s[i++];
            if ((d & 0xc0U) != 0x80U) return false;
            cp = (cp << 6U) | (uint32_t)(d & 0x3fU);
        }
        if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) return false;
        if (cp < 0x80U || (cp < 0x800U && c >= 0xe0U) || (cp < 0x10000U && c >= 0xf0U)) return false;
    }
    return true;
}

int
rawclip_latin1_to_utf8(const unsigned char *src, size_t len, char **out, size_t *out_len)
{
    size_t i, n = 0U;
    char *p;
    if (src == NULL || out == NULL || out_len == NULL || len > (SIZE_MAX - 1U) / 2U) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; i < len; i++) n += src[i] < 0x80U ? 1U : 2U;
    p = malloc(n + 1U);
    if (p == NULL) return -1;
    n = 0U;
    for (i = 0U; i < len; i++) {
        unsigned char c = src[i];
        if (c < 0x80U) p[n++] = (char)c;
        else {
            p[n++] = (char)(0xc0U | (c >> 6U));
            p[n++] = (char)(0x80U | (c & 0x3fU));
        }
    }
    p[n] = '\0';
    *out = p;
    *out_len = n;
    return 0;
}

int
rawclip_utf8_to_latin1(const char *src, size_t len, unsigned char **out, size_t *out_len)
{
    size_t i = 0U, n = 0U;
    unsigned char *p;
    if (src == NULL || out == NULL || out_len == NULL || !rawclip_utf8_valid((const unsigned char *)src, len)) {
        errno = EINVAL;
        return -1;
    }
    p = malloc(len + 1U);
    if (p == NULL) return -1;
    while (i < len) {
        unsigned char c = (unsigned char)src[i++];
        if (c < 0x80U) p[n++] = c;
        else if ((c & 0xe0U) == 0xc0U) {
            unsigned char d = (unsigned char)src[i++];
            uint32_t cp = ((uint32_t)(c & 0x1fU) << 6U) | (uint32_t)(d & 0x3fU);
            if (cp > 0xffU) { free(p); errno = EILSEQ; return -1; }
            p[n++] = (unsigned char)cp;
        } else {
            free(p);
            errno = EILSEQ;
            return -1;
        }
    }
    *out = p;
    *out_len = n;
    return 0;
}
