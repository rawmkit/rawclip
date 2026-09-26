#define _POSIX_C_SOURCE 200809L

#include "ipc.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

int
main(void)
{
    struct sockaddr_un addr;
    unsigned char header[RAWCLIP_IPC_HEADER_SIZE];
    char path[sizeof(addr.sun_path)];
    int listen_fd = -1;
    int client_fd = -1;
    int rc = 1;

    if (rawclip_ipc_validate_runtime_dir() != 0 ||
        rawclip_ipc_socket_path(path, sizeof(path), NULL) != 0) {
        perror("stale-daemon: runtime");
        return 1;
    }

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("stale-daemon: socket");
        return 1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, strlen(path) + 1U);
    (void)unlink(path);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        chmod(path, S_IRUSR | S_IWUSR) != 0 || listen(listen_fd, 1) != 0) {
        perror("stale-daemon: listen");
        goto out;
    }

    puts("ready");
    fflush(stdout);
    client_fd = accept(listen_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("stale-daemon: accept");
        goto out;
    }

    /* Simulate a pre-v2 daemon: consume the new request header, reject its
     * protocol version, and close without emitting a reply. */
    if (rawclip_read_all(client_fd, header, sizeof(header)) != 0) {
        perror("stale-daemon: read");
        goto out;
    }
    rc = 0;

out:
    if (client_fd >= 0) close(client_fd);
    if (listen_fd >= 0) close(listen_fd);
    (void)unlink(path);
    return rc;
}
