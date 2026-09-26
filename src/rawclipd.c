#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "ipc.h"
#include "rawclip.h"
#include "util.h"
#include "version.h"
#include "x11.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

struct daemon_state {
    struct rawclip_config cfg;
    struct rawclip_history history;
    struct rawclip_x11 *x11;
    char *config_path;
    char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    char lock_path[4096];
    int lock_fd;
    int listen_fd;
    bool socket_bound;
    bool paused;
};

static volatile sig_atomic_t want_stop;
static volatile sig_atomic_t want_reload;

static void
signal_handler(int sig)
{
    if (sig == SIGHUP) want_reload = 1;
    else want_stop = 1;
}

static void
usage(FILE *f)
{
    fprintf(f, "usage: rawclipd [-c path] [-C] [-v]\n");
}

static int
capture_text(void *userdata, const char *text, size_t len, uint64_t timestamp_ms)
{
    struct daemon_state *st = userdata;
    if (rawclip_history_add(&st->history, text, len, timestamp_ms, NULL) == 0)
        return RAWCLIP_CAPTURE_ADMITTED;
    if (errno == ENOSPC)
        return RAWCLIP_CAPTURE_DROPPED;
    return RAWCLIP_CAPTURE_FATAL;
}

static int
setup_socket(struct daemon_state *st)
{
    struct sockaddr_un addr;
    struct stat sb;
    int fd;
    int n;

    if (rawclip_ipc_validate_runtime_dir() != 0) return -1;
    if (rawclip_ipc_socket_path(st->socket_path, sizeof(st->socket_path), NULL) != 0) return -1;
    n = snprintf(st->lock_path, sizeof(st->lock_path), "%s.lock", st->socket_path);
    if (n < 0 || (size_t)n >= sizeof(st->lock_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    st->lock_fd = open(st->lock_path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (st->lock_fd < 0) return -1;
    if (fstat(st->lock_fd, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_uid != getuid()) {
        errno = EPERM;
        return -1;
    }
    if (fchmod(st->lock_fd, S_IRUSR | S_IWUSR) != 0) return -1;
    if (flock(st->lock_fd, LOCK_EX | LOCK_NB) != 0) return -1;

    if (unlink(st->socket_path) != 0 && errno != ENOENT) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, st->socket_path, strlen(st->socket_path) + 1U);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    st->socket_bound = true;
    if (chmod(st->socket_path, S_IRUSR | S_IWUSR) != 0 || listen(fd, 16) != 0) {
        int saved = errno;
        close(fd);
        unlink(st->socket_path);
        st->socket_bound = false;
        errno = saved;
        return -1;
    }
    st->listen_fd = fd;
    return 0;
}

static void
bound_client_io(int fd)
{
    struct timeval tv;
    memset(&tv, 0, sizeof(tv));
    tv.tv_sec = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static int
send_error(int fd, const char *message)
{
    return rawclip_ipc_send(fd, RAWCLIP_IPC_ERROR, message, (uint32_t)strlen(message));
}

static int
parse_id_frame(const struct rawclip_ipc_frame *frame, uint64_t *id)
{
    if (frame->len == 0U || memchr(frame->data, '\0', frame->len) != NULL) {
        errno = EINVAL;
        return -1;
    }
    return rawclip_parse_u64((const char *)frame->data, id);
}

static int
reload_config(struct daemon_state *st)
{
    struct rawclip_config next;
    char err[256];
    memset(&next, 0, sizeof(next));
    if (rawclip_config_load(&next, st->config_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "rawclipd: config reload failed: %s\n", err[0] ? err : strerror(errno));
        rawclip_config_destroy(&next);
        return -1;
    }
    if (rawclip_history_reconfigure(&st->history, next.max_entries,
                                    next.max_entry_bytes, next.deduplicate) != 0) {
        fprintf(stderr, "rawclipd: config reload failed: pinned history conflicts with new limits: %s\n",
                strerror(errno));
        rawclip_config_destroy(&next);
        return -1;
    }
    rawclip_config_destroy(&st->cfg);
    st->cfg = next;
    rawclip_x11_set_config(st->x11, &st->cfg);
    return 0;
}

static int
handle_client(struct daemon_state *st, int fd)
{
    struct rawclip_ipc_frame frame;
    uint64_t id;
    const struct rawclip_entry *e;
    size_t i;
    char status[384];
    struct rawclip_x11_status xstatus;

    if (rawclip_ipc_peer_is_current_user(fd) != 0) return -1;
    if (rawclip_ipc_recv(fd, &frame) != 0) return -1;
    switch (frame.type) {
    case RAWCLIP_IPC_STATUS:
        rawclip_x11_get_status(st->x11, &xstatus);
        snprintf(status, sizeof(status),
                 "paused=%d entries=%zu pinned=%zu clipboard=%d primary=%d synchronize=%s "
                 "receive_pending=%d outgoing_transfers=%zu own_clipboard=%d own_primary=%d\n",
                 st->paused ? 1 : 0, st->history.len,
                 rawclip_history_pinned_count(&st->history),
                 st->cfg.monitor_clipboard ? 1 : 0,
                 st->cfg.monitor_primary ? 1 : 0,
                 rawclip_sync_mode_name(st->cfg.sync_mode),
                 xstatus.receive_pending ? 1 : 0, xstatus.outgoing_transfers,
                 xstatus.own_clipboard ? 1 : 0, xstatus.own_primary ? 1 : 0);
        rawclip_ipc_send(fd, RAWCLIP_IPC_DATA, status, (uint32_t)strlen(status));
        break;
    case RAWCLIP_IPC_HEALTH:
        rawclip_ipc_send(fd, RAWCLIP_IPC_DATA, "ok\n", 3U);
        break;
    case RAWCLIP_IPC_LIST:
        for (i = 0U; i < st->history.len; i++) {
            unsigned char *payload = NULL;
            uint32_t payload_len = 0U;
            e = &st->history.entries[i];
            if (rawclip_ipc_pack_entry(e->id, e->timestamp_ms,
                                       e->pinned ? RAWCLIP_ENTRY_FLAG_PINNED : 0U,
                                       e->text, e->len, &payload, &payload_len) != 0) {
                rawclip_ipc_frame_destroy(&frame);
                return -1;
            }
            if (rawclip_ipc_send(fd, RAWCLIP_IPC_DATA, payload, payload_len) != 0) {
                free(payload);
                rawclip_ipc_frame_destroy(&frame);
                return -1;
            }
            free(payload);
        }
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_SHOW:
        if (parse_id_frame(&frame, &id) != 0 || (e = rawclip_history_get(&st->history, id)) == NULL) send_error(fd, "entry not found");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_DATA, e->text, (uint32_t)e->len);
        break;
    case RAWCLIP_IPC_SELECT:
        if (parse_id_frame(&frame, &id) != 0 || (e = rawclip_history_get(&st->history, id)) == NULL) send_error(fd, "entry not found");
        else if (rawclip_x11_own_clipboard(st->x11, e->text, e->len) != 0) send_error(fd, "cannot own clipboard");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_DELETE:
        if (parse_id_frame(&frame, &id) != 0 || rawclip_history_delete(&st->history, id) != 0) send_error(fd, "entry not found");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_PIN:
        if (parse_id_frame(&frame, &id) != 0 || rawclip_history_set_pinned(&st->history, id, true) != 0) send_error(fd, "entry not found");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_UNPIN:
        if (parse_id_frame(&frame, &id) != 0 || rawclip_history_set_pinned(&st->history, id, false) != 0) send_error(fd, "entry not found");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_TOGGLE_PIN:
        if (parse_id_frame(&frame, &id) != 0 || (e = rawclip_history_get(&st->history, id)) == NULL ||
            rawclip_history_set_pinned(&st->history, id, !e->pinned) != 0) send_error(fd, "entry not found");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_CLEAR_UNPINNED:
        rawclip_history_clear_unpinned(&st->history);
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_CLEAR:
        rawclip_history_clear(&st->history);
        rawclip_x11_clear_owned(st->x11);
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_PAUSE:
        st->paused = true;
        rawclip_x11_set_paused(st->x11, true);
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_RESUME:
        st->paused = false;
        rawclip_x11_set_paused(st->x11, false);
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_TOGGLE:
        st->paused = !st->paused;
        rawclip_x11_set_paused(st->x11, st->paused);
        rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    case RAWCLIP_IPC_RELOAD:
        if (reload_config(st) != 0) send_error(fd, "configuration reload failed");
        else rawclip_ipc_send(fd, RAWCLIP_IPC_OK, NULL, 0U);
        break;
    default:
        send_error(fd, "unknown request");
        break;
    }
    rawclip_ipc_frame_destroy(&frame);
    return 0;
}

int
main(int argc, char **argv)
{
    struct daemon_state st;
    struct sigaction sa;
    struct pollfd fds[2];
    char err[256] = {0};
    const char *config_path = NULL;
    bool check_config = false;
    bool show_version = false;
    int opt;
    int rc = 1;

    memset(&st, 0, sizeof(st));
    st.lock_fd = -1;
    st.listen_fd = -1;
    while ((opt = getopt(argc, argv, "c:Cv")) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'C': check_config = true; break;
        case 'v': show_version = true; break;
        default: usage(stderr); return 2;
        }
    }
    if (show_version) {
        if (check_config || config_path != NULL || optind != argc) { usage(stderr); return 2; }
        printf("rawclipd-%s\n", RAWCLIP_VERSION);
        return 0;
    }
    if (optind != argc) { usage(stderr); return 2; }
    if (check_config) {
        if (config_path == NULL) { fprintf(stderr, "rawclipd: -C requires -c path\n"); return 2; }
        if (rawclip_config_validate_file(config_path, err, sizeof(err)) != 0) {
            fprintf(stderr, "rawclipd: %s\n", err[0] ? err : strerror(errno));
            return 1;
        }
        return 0;
    }
    if (config_path != NULL) {
        st.config_path = strdup(config_path);
        if (st.config_path == NULL) {
            fprintf(stderr, "rawclipd: out of memory\n");
            goto out;
        }
    }
    if (rawclip_config_load(&st.cfg, config_path, err, sizeof(err)) != 0) {
        fprintf(stderr, "rawclipd: %s\n", err[0] ? err : strerror(errno));
        goto out;
    }
    rawclip_history_init(&st.history, st.cfg.max_entries, st.cfg.max_entry_bytes, st.cfg.deduplicate);
    st.x11 = rawclip_x11_create(&st.cfg, capture_text, &st, err, sizeof(err));
    if (st.x11 == NULL) {
        fprintf(stderr, "rawclipd: %s\n", err[0] ? err : strerror(errno));
        goto out;
    }
    if (setup_socket(&st) != 0) {
        fprintf(stderr, "rawclipd: cannot create control socket: %s\n", strerror(errno));
        goto out;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    while (!want_stop) {
        /* Synchronous Xlib requests made while handling IPC may pull events
         * into Xlib's user-space queue. Drain that queue before sleeping on
         * the connection fd so already-buffered ownership changes cannot wait
         * indefinitely for unrelated kernel activity. */
        if (rawclip_x11_dispatch(st.x11) != 0) {
            fprintf(stderr, "rawclipd: X11 event processing failed\n");
            goto out;
        }
        if (want_reload) {
            want_reload = 0;
            (void)reload_config(&st);
        }
        fds[0].fd = rawclip_x11_fd(st.x11);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = st.listen_fd;
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "rawclipd: poll: %s\n", strerror(errno));
            goto out;
        }
        if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "rawclipd: X11 connection became unavailable\n");
            goto out;
        }
        if ((fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            fprintf(stderr, "rawclipd: control socket became unavailable\n");
            goto out;
        }
        if ((fds[0].revents & POLLIN) != 0 && rawclip_x11_dispatch(st.x11) != 0) {
            fprintf(stderr, "rawclipd: X11 event processing failed\n");
            goto out;
        }
        if ((fds[1].revents & POLLIN) != 0) {
            int client = accept4(st.listen_fd, NULL, NULL, SOCK_CLOEXEC);
            if (client >= 0) {
                bound_client_io(client);
                (void)handle_client(&st, client);
                close(client);
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                fprintf(stderr, "rawclipd: accept: %s\n", strerror(errno));
                goto out;
            }
        }
    }
    rc = 0;
out:
    if (st.listen_fd >= 0) close(st.listen_fd);
    if (st.socket_bound && st.socket_path[0] != '\0') unlink(st.socket_path);
    if (st.lock_fd >= 0) close(st.lock_fd);
    rawclip_x11_destroy(st.x11);
    rawclip_history_destroy(&st.history);
    rawclip_config_destroy(&st.cfg);
    free(st.config_path);
    return rc;
}
