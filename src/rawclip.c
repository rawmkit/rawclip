#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "ipc.h"
#include "util.h"
#include "version.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

struct menu_entry {
    uint64_t id;
    char *text;
    size_t len;
};

static void
usage(FILE *f)
{
    fprintf(f,
            "usage: rawclip [-c path] COMMAND [ARG]\n"
            "       rawclip -v\n"
            "commands: menu [select|delete] list show select delete clear pause resume toggle status reload\n");
}

static int
connect_daemon(void)
{
    struct sockaddr_un addr;
    char path[sizeof(addr.sun_path)];
    int fd;
    if (rawclip_ipc_socket_path(path, sizeof(path), NULL) != 0) return -1;
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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

static int
request_once(uint16_t type, const char *arg, struct rawclip_ipc_frame *reply)
{
    int fd = connect_daemon();
    uint32_t len = arg == NULL ? 0U : (uint32_t)strlen(arg);
    if (fd < 0) return -1;
    if (rawclip_ipc_send(fd, type, arg, len) != 0 || rawclip_ipc_recv(fd, reply) != 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    close(fd);
    return 0;
}

static int
print_reply(const struct rawclip_ipc_frame *reply, bool exact)
{
    if (reply->type == RAWCLIP_IPC_ERROR) {
        fprintf(stderr, "rawclip: %.*s\n", (int)reply->len, reply->data == NULL ? (unsigned char *)"error" : reply->data);
        return 1;
    }
    if (reply->type == RAWCLIP_IPC_DATA && reply->len > 0U) {
        if (fwrite(reply->data, 1U, reply->len, stdout) != reply->len) return 1;
        if (!exact && reply->data[reply->len - 1U] != '\n') putchar('\n');
    }
    return reply->type == RAWCLIP_IPC_OK || reply->type == RAWCLIP_IPC_DATA ? 0 : 1;
}

static size_t
utf8_sequence_len(unsigned char c)
{
    if (c < 0x80U) return 1U;
    if ((c & 0xe0U) == 0xc0U) return 2U;
    if ((c & 0xf0U) == 0xe0U) return 3U;
    if ((c & 0xf8U) == 0xf0U) return 4U;
    return 1U;
}

static void
preview(FILE *f, const char *text, size_t len, size_t max_chars)
{
    size_t i = 0U, chars = 0U;
    bool space = false;

    while (i < len && chars < max_chars) {
        unsigned char c = (unsigned char)text[i];
        size_t seq = utf8_sequence_len(c);
        if (seq > len - i) seq = 1U;
        if (c < 0x20U || c == 0x7fU) {
            if (!space) fputc(' ', f);
            space = true;
            i++;
        } else {
            if (fwrite(text + i, 1U, seq, f) != seq) return;
            space = false;
            i += seq;
        }
        chars++;
    }
    if (i < len) fputs("…", f);
}

static int
list_command(void)
{
    int fd = connect_daemon();
    if (fd < 0) return -1;
    if (rawclip_ipc_send(fd, RAWCLIP_IPC_LIST, NULL, 0U) != 0) goto fail;
    for (;;) {
        struct rawclip_ipc_frame frame;
        uint64_t id, ts;
        const char *text;
        size_t len;
        if (rawclip_ipc_recv(fd, &frame) != 0) goto fail;
        if (frame.type == RAWCLIP_IPC_OK) {
            rawclip_ipc_frame_destroy(&frame);
            close(fd);
            return 0;
        }
        if (frame.type == RAWCLIP_IPC_ERROR) {
            fprintf(stderr, "rawclip: %.*s\n", (int)frame.len, frame.data);
            rawclip_ipc_frame_destroy(&frame);
            close(fd);
            return 1;
        }
        if (frame.type != RAWCLIP_IPC_DATA || rawclip_ipc_unpack_entry(frame.data, frame.len, &id, &ts, &text, &len) != 0) {
            rawclip_ipc_frame_destroy(&frame);
            errno = EPROTO;
            goto fail;
        }
        (void)ts;
        printf("%llu\t", (unsigned long long)id);
        preview(stdout, text, len, 200U);
        putchar('\n');
        rawclip_ipc_frame_destroy(&frame);
    }
fail:
    {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
}

static int
collect_entries(struct menu_entry **out, size_t *out_len)
{
    int fd = connect_daemon();
    struct menu_entry *entries = NULL;
    size_t len = 0U, cap = 0U;
    if (fd < 0) return -1;
    if (rawclip_ipc_send(fd, RAWCLIP_IPC_LIST, NULL, 0U) != 0) goto fail;
    for (;;) {
        struct rawclip_ipc_frame frame;
        uint64_t id, ts;
        const char *text;
        size_t text_len;
        if (rawclip_ipc_recv(fd, &frame) != 0) goto fail;
        if (frame.type == RAWCLIP_IPC_OK) {
            rawclip_ipc_frame_destroy(&frame);
            break;
        }
        if (frame.type != RAWCLIP_IPC_DATA || rawclip_ipc_unpack_entry(frame.data, frame.len, &id, &ts, &text, &text_len) != 0) {
            rawclip_ipc_frame_destroy(&frame);
            errno = EPROTO;
            goto fail;
        }
        if (len == cap) {
            size_t next = cap == 0U ? 16U : cap * 2U;
            struct menu_entry *p = realloc(entries, next * sizeof(*p));
            if (p == NULL) { rawclip_ipc_frame_destroy(&frame); goto fail; }
            entries = p;
            cap = next;
        }
        entries[len].id = id;
        entries[len].text = rawclip_xstrndup(text, text_len);
        entries[len].len = text_len;
        if (entries[len].text == NULL) { rawclip_ipc_frame_destroy(&frame); goto fail; }
        len++;
        (void)ts;
        rawclip_ipc_frame_destroy(&frame);
    }
    close(fd);
    *out = entries;
    *out_len = len;
    return 0;
fail:
    {
        size_t i;
        int saved = errno;
        for (i = 0U; i < len; i++) free(entries[i].text);
        free(entries);
        close(fd);
        errno = saved;
        return -1;
    }
}

static void
free_entries(struct menu_entry *entries, size_t len)
{
    size_t i;
    for (i = 0U; i < len; i++) free(entries[i].text);
    free(entries);
}

enum menu_action {
    MENU_SELECT = 0,
    MENU_DELETE,
};

static int
menu_command(const struct rawclip_config *cfg, enum menu_action action)
{
    struct menu_entry *entries = NULL;
    size_t len = 0U, i;
    int inpipe[2] = {-1, -1};
    int outpipe[2] = {-1, -1};
    pid_t pid;
    FILE *input = NULL;
    char selected[4096];
    ssize_t n;
    int status;
    uint64_t id;
    char idbuf[32];
    struct rawclip_ipc_frame reply;
    char action_prompt[256];
    const char *prompt = cfg->menu_prompt;

    if (action == MENU_DELETE) {
        int pn = snprintf(action_prompt, sizeof(action_prompt), "%s delete", cfg->menu_prompt);
        if (pn < 0 || (size_t)pn >= sizeof(action_prompt)) { errno = ENAMETOOLONG; return -1; }
        prompt = action_prompt;
    }

    if (collect_entries(&entries, &len) != 0) return -1;
    if (len == 0U) {
        fprintf(stderr, "rawclip: history is empty\n");
        free_entries(entries, len);
        return 0;
    }
    if (pipe(inpipe) != 0 || pipe(outpipe) != 0) goto fail;
    pid = fork();
    if (pid < 0) goto fail;
    if (pid == 0) {
        char *const argv[] = {cfg->menu_command, (char *)"-M", (char *)"select",
                              (char *)"-p", (char *)prompt, NULL};
        dup2(inpipe[0], STDIN_FILENO);
        dup2(outpipe[1], STDOUT_FILENO);
        close(inpipe[0]); close(inpipe[1]); close(outpipe[0]); close(outpipe[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(inpipe[0]); inpipe[0] = -1;
    close(outpipe[1]); outpipe[1] = -1;
    input = fdopen(inpipe[1], "w");
    if (input == NULL) goto parent_fail;
    inpipe[1] = -1;
    for (i = 0U; i < len; i++) {
        fprintf(input, "%llu\t", (unsigned long long)entries[i].id);
        preview(input, entries[i].text, entries[i].len, cfg->menu_preview_chars);
        fputc('\n', input);
    }
    if (fclose(input) != 0) { input = NULL; goto parent_fail; }
    input = NULL;
    n = read(outpipe[0], selected, sizeof(selected) - 1U);
    close(outpipe[0]); outpipe[0] = -1;
    if (waitpid(pid, &status, 0) < 0) goto fail;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
        free_entries(entries, len);
        return 0;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        errno = EIO;
        goto fail;
    }
    if (n <= 0) {
        errno = EPROTO;
        goto fail;
    }
    selected[n] = '\0';
    if (strchr(selected, '\t') != NULL) *strchr(selected, '\t') = '\0';
    if (strchr(selected, '\n') != NULL) *strchr(selected, '\n') = '\0';
    if (rawclip_parse_u64(selected, &id) != 0) { errno = EPROTO; goto fail; }
    snprintf(idbuf, sizeof(idbuf), "%llu", (unsigned long long)id);
    if (request_once(action == MENU_DELETE ? RAWCLIP_IPC_DELETE : RAWCLIP_IPC_SELECT,
                     idbuf, &reply) != 0) goto fail;
    status = print_reply(&reply, false);
    rawclip_ipc_frame_destroy(&reply);
    free_entries(entries, len);
    return status;

parent_fail:
    if (input != NULL) fclose(input);
    if (inpipe[1] >= 0) close(inpipe[1]);
    if (outpipe[0] >= 0) close(outpipe[0]);
    (void)kill(pid, SIGTERM);
    (void)waitpid(pid, NULL, 0);
fail:
    if (inpipe[0] >= 0) close(inpipe[0]);
    if (inpipe[1] >= 0) close(inpipe[1]);
    if (outpipe[0] >= 0) close(outpipe[0]);
    if (outpipe[1] >= 0) close(outpipe[1]);
    free_entries(entries, len);
    return -1;
}

int
main(int argc, char **argv)
{
    const char *config_path = NULL;
    struct rawclip_config cfg;
    struct rawclip_ipc_frame reply;
    char err[256] = {0};
    const char *cmd;
    const char *arg = NULL;
    uint16_t type = 0;
    bool exact = false;
    int opt;
    int rc;

    (void)signal(SIGPIPE, SIG_IGN);
    while ((opt = getopt(argc, argv, "c:v")) != -1) {
        switch (opt) {
        case 'c': config_path = optarg; break;
        case 'v': printf("rawclip-%s\n", RAWCLIP_VERSION); return 0;
        default: usage(stderr); return 2;
        }
    }
    if (optind >= argc) { usage(stderr); return 2; }
    cmd = argv[optind++];
    if (optind < argc) arg = argv[optind++];
    if (optind != argc) { usage(stderr); return 2; }

    if (strcmp(cmd, "menu") == 0) {
        enum menu_action action = MENU_SELECT;
        if (arg != NULL) {
            if (strcmp(arg, "select") == 0) action = MENU_SELECT;
            else if (strcmp(arg, "delete") == 0) action = MENU_DELETE;
            else { usage(stderr); return 2; }
        }
        memset(&cfg, 0, sizeof(cfg));
        if (rawclip_config_load(&cfg, config_path, err, sizeof(err)) != 0) {
            fprintf(stderr, "rawclip: %s\n", err[0] ? err : strerror(errno));
            return 1;
        }
        rc = menu_command(&cfg, action);
        rawclip_config_destroy(&cfg);
        if (rc < 0) fprintf(stderr, "rawclip: menu: %s\n", strerror(errno));
        return rc < 0 ? 1 : rc;
    }
    if (strcmp(cmd, "list") == 0) {
        if (arg != NULL) { usage(stderr); return 2; }
        rc = list_command();
        if (rc < 0) fprintf(stderr, "rawclip: list: %s\n", strerror(errno));
        return rc < 0 ? 1 : rc;
    }
#define CMD0(NAME, TYPE) if (strcmp(cmd, NAME) == 0) { if (arg != NULL) { usage(stderr); return 2; } type = TYPE; }
#define CMD1(NAME, TYPE) if (strcmp(cmd, NAME) == 0) { if (arg == NULL) { usage(stderr); return 2; } type = TYPE; }
    CMD1("show", RAWCLIP_IPC_SHOW)
    else CMD1("select", RAWCLIP_IPC_SELECT)
    else CMD1("delete", RAWCLIP_IPC_DELETE)
    else CMD0("clear", RAWCLIP_IPC_CLEAR)
    else CMD0("pause", RAWCLIP_IPC_PAUSE)
    else CMD0("resume", RAWCLIP_IPC_RESUME)
    else CMD0("toggle", RAWCLIP_IPC_TOGGLE)
    else CMD0("status", RAWCLIP_IPC_STATUS)
    else CMD0("reload", RAWCLIP_IPC_RELOAD)
    else { usage(stderr); return 2; }
#undef CMD0
#undef CMD1
    if (type == RAWCLIP_IPC_SHOW) exact = true;
    if (request_once(type, arg, &reply) != 0) {
        fprintf(stderr, "rawclip: %s\n", strerror(errno));
        return 1;
    }
    rc = print_reply(&reply, exact);
    rawclip_ipc_frame_destroy(&reply);
    return rc;
}
