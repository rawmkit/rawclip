#ifndef RAWCLIP_X11_H
#define RAWCLIP_X11_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*rawclip_capture_fn)(void *userdata, const char *text, size_t len,
                                  uint64_t timestamp_ms);

struct rawclip_x11;

struct rawclip_x11 *rawclip_x11_create(const struct rawclip_config *cfg,
                                       rawclip_capture_fn capture,
                                       void *userdata, char *err,
                                       size_t err_size);
void rawclip_x11_destroy(struct rawclip_x11 *x);
int rawclip_x11_fd(const struct rawclip_x11 *x);
int rawclip_x11_dispatch(struct rawclip_x11 *x);
void rawclip_x11_set_paused(struct rawclip_x11 *x, bool paused);
void rawclip_x11_set_config(struct rawclip_x11 *x,
                            const struct rawclip_config *cfg);
int rawclip_x11_own_clipboard(struct rawclip_x11 *x, const char *text,
                              size_t len);
void rawclip_x11_clear_owned(struct rawclip_x11 *x);

#endif
