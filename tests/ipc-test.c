#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

int
main(void)
{
    int sv[2];
    struct rawclip_ipc_frame frame;
    char path[256];
    char other[256];
    char runtime[] = "/tmp/rawclip-ipc-test.XXXXXX";
    unsigned char bad[12] = {0};

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(rawclip_ipc_peer_is_current_user(sv[0]) == 0);
    assert(rawclip_ipc_peer_is_current_user(sv[1]) == 0);
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

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    memset(bad, 0, sizeof(bad));
    memcpy(bad, "RCLP", 4U);
    {
        uint16_t version = htons((uint16_t)(RAWCLIP_IPC_VERSION + 1U));
        memcpy(bad + 4U, &version, sizeof(version));
    }
    assert(write(sv[0], bad, sizeof(bad)) == (ssize_t)sizeof(bad));
    errno = 0;
    assert(rawclip_ipc_recv(sv[1], &frame) == -1);
    assert(errno == EPROTONOSUPPORT);
    close(sv[0]);
    close(sv[1]);

    {
        unsigned char *payload = NULL;
        uint32_t payload_len = 0U;
        uint64_t id, ts;
        uint32_t flags;
        const char *text;
        size_t text_len;
        assert(rawclip_ipc_pack_entry(42U, 99U, RAWCLIP_ENTRY_FLAG_PINNED, "abc", 3U, &payload, &payload_len) == 0);
        assert(rawclip_ipc_unpack_entry(payload, payload_len, &id, &ts, &flags, &text, &text_len) == 0);
        assert(id == 42U && ts == 99U && flags == RAWCLIP_ENTRY_FLAG_PINNED &&
               text_len == 3U && memcmp(text, "abc", 3U) == 0);
        free(payload);
    }

    assert(mkdtemp(runtime) != NULL);
    assert(chmod(runtime, 0700) == 0);
    assert(setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
    assert(rawclip_ipc_validate_runtime_dir() == 0);
    assert(rawclip_ipc_socket_path(path, sizeof(path), ":0.0") == 0);
    assert(rawclip_ipc_socket_path(other, sizeof(other), ":0.1") == 0);
    assert(strcmp(path, other) == 0);
    assert(rawclip_ipc_socket_path(other, sizeof(other), ":0") == 0);
    assert(strcmp(path, other) == 0);
    {
        char expected[512];
        assert(snprintf(expected, sizeof(expected), "%s/rawclip-_0.sock", runtime) > 0);
        assert(strcmp(path, expected) == 0);
    }
    assert(rawclip_ipc_socket_path(other, sizeof(other), "unix/:0") == 0);
    assert(strcmp(path, other) != 0);
    assert(strstr(other, "rawclip-unix~2f_0.sock") != NULL);
    assert(rawclip_ipc_socket_path(other, sizeof(other), "unix_:0") == 0);
    assert(strstr(other, "rawclip-unix~5f_0.sock") != NULL);
    assert(chmod(runtime, 0770) == 0);
    errno = 0;
    assert(rawclip_ipc_validate_runtime_dir() == -1);
    assert(errno == EPERM);
    assert(chmod(runtime, 0700) == 0);
    assert(rmdir(runtime) == 0);
    return 0;
}
