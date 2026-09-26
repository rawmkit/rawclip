#define _POSIX_C_SOURCE 200809L

#include "ipc.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int
connect_daemon(void)
{
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];
    int fd;

    if (rawclip_ipc_validate_runtime_dir() != 0 ||
        rawclip_ipc_socket_path(path, sizeof(path), NULL) != 0) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, strlen(path) + 1U);
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

static void
sleep_ms(unsigned int ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000U);
    ts.tv_nsec = (long)(ms % 1000U) * 1000000L;
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
}

int
main(int argc, char **argv)
{
    int fd;

    if (argc != 2) return 2;
    if (strcmp(argv[1], "path") == 0) {
        char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
        if (rawclip_ipc_socket_path(path, sizeof(path), NULL) != 0) {
            perror("ipc-probe: path");
            return 1;
        }
        puts(path);
        return 0;
    }
    fd = connect_daemon();
    if (fd < 0) {
        perror("ipc-probe: connect");
        return 1;
    }
    if (strcmp(argv[1], "abort-list") == 0) {
        if (rawclip_ipc_send(fd, RAWCLIP_IPC_LIST, NULL, 0U) != 0) {
            perror("ipc-probe: send");
            close(fd);
            return 1;
        }
        (void)shutdown(fd, SHUT_RDWR);
        close(fd);
        return 0;
    }
    if (strcmp(argv[1], "partial-frame") == 0) {
        if (write(fd, "RCLP", 4U) != 4) {
            perror("ipc-probe: write");
            close(fd);
            return 1;
        }
        sleep_ms(1250U);
        close(fd);
        return 0;
    }
    close(fd);
    return 2;
}
