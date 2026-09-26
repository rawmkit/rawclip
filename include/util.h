#ifndef RAWCLIP_UTIL_H
#define RAWCLIP_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int rawclip_set_cloexec(int fd);
int rawclip_set_nonblock(int fd);
uint64_t rawclip_now_ms(void);
int rawclip_parse_u64(const char *s, uint64_t *out);
char *rawclip_xstrndup(const char *s, size_t n);
void rawclip_memzero(void *buf, size_t len);
int rawclip_write_all(int fd, const void *buf, size_t len);
int rawclip_read_all(int fd, void *buf, size_t len);
bool rawclip_utf8_valid(const unsigned char *s, size_t len);
int rawclip_latin1_to_utf8(const unsigned char *src, size_t len, char **out, size_t *out_len);
int rawclip_utf8_to_latin1(const char *src, size_t len, unsigned char **out, size_t *out_len);

#endif
