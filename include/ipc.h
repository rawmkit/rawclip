#ifndef RAWCLIP_IPC_H
#define RAWCLIP_IPC_H

#include <stddef.h>
#include <stdint.h>

#define RAWCLIP_IPC_VERSION 1U
#define RAWCLIP_IPC_HEADER_SIZE 12U

enum rawclip_ipc_type {
    RAWCLIP_IPC_STATUS = 1,
    RAWCLIP_IPC_LIST = 2,
    RAWCLIP_IPC_SHOW = 3,
    RAWCLIP_IPC_SELECT = 4,
    RAWCLIP_IPC_DELETE = 5,
    RAWCLIP_IPC_CLEAR = 6,
    RAWCLIP_IPC_PAUSE = 7,
    RAWCLIP_IPC_RESUME = 8,
    RAWCLIP_IPC_TOGGLE = 9,
    RAWCLIP_IPC_RELOAD = 10,
    RAWCLIP_IPC_HEALTH = 11,
    RAWCLIP_IPC_OK = 100,
    RAWCLIP_IPC_ERROR = 101,
    RAWCLIP_IPC_DATA = 102,
};

struct rawclip_ipc_frame {
    uint16_t type;
    uint32_t len;
    unsigned char *data;
};

int rawclip_ipc_send(int fd, uint16_t type, const void *data, uint32_t len);
int rawclip_ipc_recv(int fd, struct rawclip_ipc_frame *frame);
void rawclip_ipc_frame_destroy(struct rawclip_ipc_frame *frame);
int rawclip_ipc_socket_path(char *buf, size_t size, const char *display);
int rawclip_ipc_pack_entry(uint64_t id, uint64_t timestamp_ms, const char *text,
                           size_t len, unsigned char **out, uint32_t *out_len);
int rawclip_ipc_unpack_entry(const unsigned char *data, uint32_t len, uint64_t *id,
                             uint64_t *timestamp_ms, const char **text, size_t *text_len);

#endif
