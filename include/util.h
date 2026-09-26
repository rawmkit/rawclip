#ifndef RAWCLIP_UTIL_H
#define RAWCLIP_UTIL_H

#include <stddef.h>
#include <stdint.h>

int rawclip_set_cloexec(int fd);
int rawclip_set_nonblock(int fd);
uint64_t rawclip_now_ms(void);
int rawclip_parse_u64(const char *s, uint64_t *out);
char *rawclip_xstrndup(const char *s, size_t n);
int rawclip_write_all(int fd, const void *buf, size_t len);
int rawclip_read_all(int fd, void *buf, size_t len);

#endif
