#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int
main(void)
{
    int sv[2];
    struct rawclip_ipc_frame frame;
    char path[256];
    unsigned char bad[12] = {0};

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(rawclip_ipc_send(sv[0], RAWCLIP_IPC_DATA, "hello", 5U) == 0);
    assert(rawclip_ipc_recv(sv[1], &frame) == 0);
    assert(frame.type == RAWCLIP_IPC_DATA);
    assert(frame.len == 5U);
    assert(memcmp(frame.data, "hello", 5U) == 0);
    rawclip_ipc_frame_destroy(&frame);

    assert(write(sv[0], bad, sizeof(bad)) == (ssize_t)sizeof(bad));
    errno = 0;
    assert(rawclip_ipc_recv(sv[1], &frame) == -1);
    assert(errno == EPROTO);
    close(sv[0]);
    close(sv[1]);

    assert(setenv("XDG_RUNTIME_DIR", "/tmp/runtime-test", 1) == 0);
    assert(rawclip_ipc_socket_path(path, sizeof(path), ":0.0") == 0);
    assert(strcmp(path, "/tmp/runtime-test/rawclip-_0_0.sock") == 0);
    return 0;
}
