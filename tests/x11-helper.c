#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stop;

#define RAWCLIP_TEST_MAX_BYTES (1024U * 1024U)

static void
on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static size_t
server_direct_bytes(Display *dpy)
{
    long max_request = XMaxRequestSize(dpy);
    return max_request > 1024 ? ((size_t)max_request - 256U) * 4U : 65536U;
}

static int
offer(const char *text, size_t text_len, const char *hint_value,
      bool broken_hint, const char *class_name)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win;
    Window send_requestor = None;
    Atom clipboard, targets, utf8, sensitive, incr;
    Atom send_property = None;
    size_t send_offset = 0U;
    size_t direct_bytes;
    unsigned long text_requests = 0U;
    unsigned long hint_requests = 0U;
    struct sigaction sa;
    bool send_active = false;

    if (dpy == NULL) return 2;
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    if (class_name != NULL) {
        XClassHint hint;
        hint.res_name = (char *)"rawclip-test-owner";
        hint.res_class = (char *)class_name;
        XSetClassHint(dpy, win, &hint);
    }
    clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    targets = XInternAtom(dpy, "TARGETS", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    sensitive = XInternAtom(dpy, "x-kde-passwordManagerHint", False);
    incr = XInternAtom(dpy, "INCR", False);
    direct_bytes = server_direct_bytes(dpy);
    XSetSelectionOwner(dpy, clipboard, win, CurrentTime);
    XFlush(dpy);
    if (XGetSelectionOwner(dpy, clipboard) != win) return 3;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    while (!stop) {
        while (XPending(dpy) > 0) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == SelectionRequest) {
                XSelectionRequestEvent *req = &ev.xselectionrequest;
                XSelectionEvent out;
                Atom property = req->property == None ? req->target : req->property;
                bool ok = false;
                memset(&out, 0, sizeof(out));
                out.type = SelectionNotify;
                out.display = req->display;
                out.requestor = req->requestor;
                out.selection = req->selection;
                out.target = req->target;
                out.time = req->time;
                out.property = None;
                if (req->target == targets) {
                    Atom list[3];
                    int n = 0;
                    list[n++] = targets;
                    list[n++] = utf8;
                    if (hint_value != NULL || broken_hint) list[n++] = sensitive;
                    XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char *)list, n);
                    ok = true;
                } else if (req->target == utf8) {
                    text_requests++;
                    if (text_len <= direct_bytes) {
                        XChangeProperty(dpy, req->requestor, property, utf8, 8,
                                        PropModeReplace, (const unsigned char *)text,
                                        (int)text_len);
                        ok = true;
                    } else if (!send_active) {
                        unsigned long total = (unsigned long)text_len;
                        send_active = true;
                        send_requestor = req->requestor;
                        send_property = property;
                        send_offset = 0U;
                        XSelectInput(dpy, send_requestor,
                                     PropertyChangeMask | StructureNotifyMask);
                        XChangeProperty(dpy, send_requestor, send_property, incr, 32,
                                        PropModeReplace, (unsigned char *)&total, 1);
                        ok = true;
                    }
                } else if (req->target == sensitive &&
                           (hint_value != NULL || broken_hint)) {
                    hint_requests++;
                    if (!broken_hint && hint_value != NULL) {
                        XChangeProperty(dpy, req->requestor, property, utf8, 8,
                                        PropModeReplace,
                                        (const unsigned char *)hint_value,
                                        (int)strlen(hint_value));
                        ok = true;
                    }
                }
                if (ok) out.property = property;
                XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&out);
                XFlush(dpy);
            } else if (ev.type == PropertyNotify && send_active &&
                       ev.xproperty.window == send_requestor &&
                       ev.xproperty.atom == send_property &&
                       ev.xproperty.state == PropertyDelete) {
                if (send_offset < text_len) {
                    size_t left = text_len - send_offset;
                    size_t chunk = left < direct_bytes ? left : direct_bytes;
                    XChangeProperty(dpy, send_requestor, send_property, utf8, 8,
                                    PropModeReplace,
                                    (const unsigned char *)text + send_offset,
                                    (int)chunk);
                    send_offset += chunk;
                } else {
                    XChangeProperty(dpy, send_requestor, send_property, utf8, 8,
                                    PropModeReplace, (const unsigned char *)"", 0);
                    XSelectInput(dpy, send_requestor, NoEventMask);
                    send_active = false;
                    send_requestor = None;
                    send_property = None;
                }
                XFlush(dpy);
            } else if (ev.type == DestroyNotify && send_active &&
                       ev.xdestroywindow.window == send_requestor) {
                send_active = false;
                send_requestor = None;
                send_property = None;
            } else if (ev.type == SelectionClear) {
                stop = 1;
            }
        }
        {
            struct timespec ts = {0, 1000000L};
            (void)nanosleep(&ts, NULL);
        }
    }
    printf("text_requests=%lu\n", text_requests);
    printf("hint_requests=%lu\n", hint_requests);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

static int
paste(bool abort_incr, bool string_target)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win;
    Atom clipboard, utf8, target, property, incr;
    XEvent ev;
    bool incremental = false;

    if (dpy == NULL) return 2;
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    XSelectInput(dpy, win, PropertyChangeMask);
    clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    target = string_target ? XA_STRING : utf8;
    incr = XInternAtom(dpy, "INCR", False);
    property = XInternAtom(dpy, "_RAWCLIP_TEST_PASTE", False);
    XConvertSelection(dpy, clipboard, target, property, win, CurrentTime);
    XFlush(dpy);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) break;
    }
    if (ev.xselection.property == None) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 3;
    }
    {
        Atom actual;
        int format;
        unsigned long nitems, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, win, property, 0, (long)((RAWCLIP_TEST_MAX_BYTES + 3U) / 4U + 16U), True,
                               AnyPropertyType, &actual, &format, &nitems, &after,
                               &data) != Success) {
            XDestroyWindow(dpy, win);
            XCloseDisplay(dpy);
            return 4;
        }
        if (actual == incr && format == 32) {
            incremental = true;
        } else if (actual == target && format == 8 && after == 0U) {
            if (data != NULL && fwrite(data, 1U, nitems, stdout) != nitems) {
                XFree(data);
                XDestroyWindow(dpy, win);
                XCloseDisplay(dpy);
                return 5;
            }
        } else {
            if (data != NULL) XFree(data);
            XDestroyWindow(dpy, win);
            XCloseDisplay(dpy);
            return 4;
        }
        if (data != NULL) XFree(data);
    }

    if (incremental && abort_incr) {
        XSync(dpy, False);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 0;
    }
    while (incremental) {
        for (;;) {
            XNextEvent(dpy, &ev);
            if (ev.type == PropertyNotify && ev.xproperty.window == win &&
                ev.xproperty.atom == property && ev.xproperty.state == PropertyNewValue)
                break;
        }
        {
            Atom actual;
            int format;
            unsigned long nitems, after;
            unsigned char *data = NULL;
            if (XGetWindowProperty(dpy, win, property, 0, (long)((RAWCLIP_TEST_MAX_BYTES + 3U) / 4U + 16U), True,
                                   AnyPropertyType, &actual, &format, &nitems,
                                   &after, &data) != Success || actual != target ||
                format != 8 || after != 0U) {
                if (data != NULL) XFree(data);
                XDestroyWindow(dpy, win);
                XCloseDisplay(dpy);
                return 4;
            }
            if (nitems == 0U) {
                if (data != NULL) XFree(data);
                break;
            }
            if (data == NULL || fwrite(data, 1U, nitems, stdout) != nitems) {
                if (data != NULL) XFree(data);
                XDestroyWindow(dpy, win);
                XCloseDisplay(dpy);
                return 5;
            }
            XFree(data);
        }
    }
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

static int
probe_owner(void)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win;
    Atom clipboard, targets, timestamp, utf8, property, unknown;
    XEvent ev;
    Atom actual;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    bool have_targets = false, have_timestamp = false;
    bool have_utf8 = false, have_string = false;
    unsigned long i;

    if (dpy == NULL) return 2;
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    targets = XInternAtom(dpy, "TARGETS", False);
    timestamp = XInternAtom(dpy, "TIMESTAMP", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    property = XInternAtom(dpy, "_RAWCLIP_TEST_PROBE", False);
    unknown = XInternAtom(dpy, "_RAWCLIP_TEST_UNSUPPORTED", False);

    XConvertSelection(dpy, clipboard, targets, property, win, CurrentTime);
    XFlush(dpy);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) break;
    }
    if (ev.xselection.property == None ||
        XGetWindowProperty(dpy, win, property, 0, 64, True, XA_ATOM, &actual,
                           &format, &nitems, &after, &data) != Success ||
        actual != XA_ATOM || format != 32 || after != 0U || data == NULL) {
        if (data != NULL) XFree(data);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 3;
    }
    for (i = 0U; i < nitems; i++) {
        Atom a = ((Atom *)data)[i];
        if (a == targets) have_targets = true;
        else if (a == timestamp) have_timestamp = true;
        else if (a == utf8) have_utf8 = true;
        else if (a == XA_STRING) have_string = true;
    }
    XFree(data);
    data = NULL;
    if (!have_targets || !have_timestamp || !have_utf8 || !have_string) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 4;
    }

    XConvertSelection(dpy, clipboard, timestamp, property, win, CurrentTime);
    XFlush(dpy);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) break;
    }
    if (ev.xselection.property == None ||
        XGetWindowProperty(dpy, win, property, 0, 1, True, XA_INTEGER, &actual,
                           &format, &nitems, &after, &data) != Success ||
        actual != XA_INTEGER || format != 32 || nitems != 1U || after != 0U) {
        if (data != NULL) XFree(data);
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 5;
    }
    if (data != NULL) XFree(data);

    XConvertSelection(dpy, clipboard, unknown, property, win, CurrentTime);
    XFlush(dpy);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) break;
    }
    if (ev.xselection.property != None) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        return 6;
    }

    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

static int
parse_size(const char *s, size_t *out)
{
    char *end = NULL;
    unsigned long long v;
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v == 0U || v > 1024U * 1024U) return -1;
    *out = (size_t)v;
    return 0;
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "ping") == 0) {
        Display *dpy = XOpenDisplay(NULL);
        if (dpy == NULL) return 1;
        XCloseDisplay(dpy);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "paste") == 0) return paste(false, false);
    if (argc == 2 && strcmp(argv[1], "paste-string") == 0) return paste(false, true);
    if (argc == 2 && strcmp(argv[1], "paste-abort") == 0) return paste(true, false);
    if (argc == 2 && strcmp(argv[1], "probe-owner") == 0) return probe_owner();
    if (argc == 3 && strcmp(argv[1], "offer-size") == 0) {
        size_t len;
        char *text;
        int rc;
        if (parse_size(argv[2], &len) != 0) return 2;
        text = malloc(len + 1U);
        if (text == NULL) return 2;
        memset(text, 'L', len);
        text[len] = '\0';
        rc = offer(text, len, NULL, false, NULL);
        free(text);
        return rc;
    }
    if (argc >= 3 && strcmp(argv[1], "offer") == 0) {
        const char *hint_value = NULL;
        bool broken_hint = false;
        const char *class_name = NULL;
        int i;
        for (i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--secret") == 0) hint_value = "secret";
            else if (strcmp(argv[i], "--hint") == 0 && i + 1 < argc) hint_value = argv[++i];
            else if (strcmp(argv[i], "--broken-hint") == 0) broken_hint = true;
            else if (strcmp(argv[i], "--class") == 0 && i + 1 < argc) class_name = argv[++i];
            else return 2;
        }
        if (broken_hint && hint_value != NULL) return 2;
        return offer(argv[2], strlen(argv[2]), hint_value, broken_hint, class_name);
    }
    return 2;
}
