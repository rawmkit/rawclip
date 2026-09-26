#include "ipc.h"
#include "rawclip.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t
read_u16(const unsigned char *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return ntohs(v);
}

static uint32_t
read_u32(const unsigned char *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return ntohl(v);
}

int
rawclip_ipc_send(int fd, uint16_t type, const void *data, uint32_t len)
{
    unsigned char header[RAWCLIP_IPC_HEADER_SIZE];
    uint16_t v16;
    uint32_t v32;

    if (len > RAWCLIP_IPC_MAX_FRAME || (len > 0U && data == NULL)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(header, "RCLP", 4U);
    v16 = htons((uint16_t)RAWCLIP_IPC_VERSION);
    memcpy(header + 4U, &v16, sizeof(v16));
    v16 = htons(type);
    memcpy(header + 6U, &v16, sizeof(v16));
    v32 = htonl(len);
    memcpy(header + 8U, &v32, sizeof(v32));

    if (rawclip_write_all(fd, header, sizeof(header)) != 0) {
        return -1;
    }
    if (len > 0U && rawclip_write_all(fd, data, len) != 0) {
        return -1;
    }
    return 0;
}

int
rawclip_ipc_recv(int fd, struct rawclip_ipc_frame *frame)
{
    unsigned char header[RAWCLIP_IPC_HEADER_SIZE];
    uint32_t len;

    if (frame == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    if (rawclip_read_all(fd, header, sizeof(header)) != 0) {
        return -1;
    }
    if (memcmp(header, "RCLP", 4U) != 0 || read_u16(header + 4U) != RAWCLIP_IPC_VERSION) {
        errno = EPROTO;
        return -1;
    }
    len = read_u32(header + 8U);
    if (len > RAWCLIP_IPC_MAX_FRAME) {
        errno = EMSGSIZE;
        return -1;
    }
    frame->type = read_u16(header + 6U);
    frame->len = len;
    if (len == 0U) {
        return 0;
    }
    frame->data = malloc((size_t)len + 1U);
    if (frame->data == NULL) {
        return -1;
    }
    if (rawclip_read_all(fd, frame->data, len) != 0) {
        rawclip_ipc_frame_destroy(frame);
        return -1;
    }
    frame->data[len] = '\0';
    return 0;
}

void
rawclip_ipc_frame_destroy(struct rawclip_ipc_frame *frame)
{
    if (frame == NULL) {
        return;
    }
    free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

int
rawclip_ipc_socket_path(char *buf, size_t size, const char *display)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *d = display != NULL ? display : getenv("DISPLAY");
    char clean[96];
    size_t i, j = 0U;
    int n;

    if (runtime == NULL || runtime[0] != '/' || d == NULL || *d == '\0') {
        errno = ENOENT;
        return -1;
    }
    for (i = 0U; d[i] != '\0' && j + 1U < sizeof(clean); i++) {
        unsigned char c = (unsigned char)d[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            clean[j++] = (char)c;
        } else {
            clean[j++] = '_';
        }
    }
    clean[j] = '\0';
    n = snprintf(buf, size, "%s/rawclip-%s.sock", runtime, clean);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}
