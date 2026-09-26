#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "ipc.h"
#include "rawclip.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
rawclip_ipc_validate_runtime_dir(void)
{
    const char *path = getenv("XDG_RUNTIME_DIR");
    struct stat sb;

    if (path == NULL || path[0] != '/' || stat(path, &sb) != 0) {
        if (path == NULL || path[0] != '/') errno = ENOENT;
        return -1;
    }
    if (!S_ISDIR(sb.st_mode) || sb.st_uid != getuid() ||
        (sb.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

int
rawclip_ipc_peer_is_current_user(int fd)
{
#ifdef SO_PEERCRED
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0) return -1;
    if (len != sizeof(cred) || cred.uid != getuid()) {
        errno = EPERM;
        return -1;
    }
    return 0;
#else
    (void)fd;
    errno = ENOTSUP;
    return -1;
#endif
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
    if (d[i] != '\0') {
        errno = ENAMETOOLONG;
        return -1;
    }
    clean[j] = '\0';
    n = snprintf(buf, size, "%s/rawclip-%s.sock", runtime, clean);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}


static uint64_t
host_to_be64(uint64_t v)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(v >> 32U))) |
           ((uint64_t)htonl((uint32_t)(v & 0xffffffffU)) << 32U);
#else
    return v;
#endif
}

static uint64_t
be64_to_host(uint64_t v)
{
    return host_to_be64(v);
}

int
rawclip_ipc_pack_entry(uint64_t id, uint64_t timestamp_ms, uint32_t flags,
                       const char *text, size_t len, unsigned char **out,
                       uint32_t *out_len)
{
    unsigned char *p;
    uint64_t v64;
    uint32_t v32;
    if (text == NULL || out == NULL || out_len == NULL || len > UINT32_MAX - 20U) {
        errno = EINVAL;
        return -1;
    }
    p = malloc(len + 20U);
    if (p == NULL) return -1;
    v64 = host_to_be64(id);
    memcpy(p, &v64, 8U);
    v64 = host_to_be64(timestamp_ms);
    memcpy(p + 8U, &v64, 8U);
    v32 = htonl(flags);
    memcpy(p + 16U, &v32, 4U);
    memcpy(p + 20U, text, len);
    *out = p;
    *out_len = (uint32_t)(len + 20U);
    return 0;
}

int
rawclip_ipc_unpack_entry(const unsigned char *data, uint32_t len, uint64_t *id,
                         uint64_t *timestamp_ms, uint32_t *flags,
                         const char **text, size_t *text_len)
{
    uint64_t v64;
    uint32_t v32;
    if (data == NULL || len < 20U || id == NULL || timestamp_ms == NULL ||
        flags == NULL || text == NULL || text_len == NULL) {
        errno = EINVAL;
        return -1;
    }
    memcpy(&v64, data, 8U);
    *id = be64_to_host(v64);
    memcpy(&v64, data + 8U, 8U);
    *timestamp_ms = be64_to_host(v64);
    memcpy(&v32, data + 16U, 4U);
    *flags = ntohl(v32);
    *text = (const char *)(data + 20U);
    *text_len = (size_t)len - 20U;
    return 0;
}
