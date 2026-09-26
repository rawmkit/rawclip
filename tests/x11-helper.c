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

static void
on_signal(int sig)
{
    (void)sig;
    stop = 1;
}

static int
offer(const char *text, bool secret, const char *class_name)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win;
    Atom clipboard, targets, utf8, sensitive;
    unsigned long text_requests = 0U;
    struct sigaction sa;
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
                    if (secret) list[n++] = sensitive;
                    XChangeProperty(dpy, req->requestor, property, XA_ATOM, 32,
                                    PropModeReplace, (unsigned char *)list, n);
                    ok = true;
                } else if (req->target == utf8) {
                    text_requests++;
                    XChangeProperty(dpy, req->requestor, property, utf8, 8,
                                    PropModeReplace, (const unsigned char *)text,
                                    (int)strlen(text));
                    ok = true;
                } else if (req->target == sensitive && secret) {
                    const char value[] = "secret";
                    XChangeProperty(dpy, req->requestor, property, utf8, 8,
                                    PropModeReplace, (const unsigned char *)value, 6);
                    ok = true;
                }
                if (ok) out.property = property;
                XSendEvent(dpy, req->requestor, False, 0, (XEvent *)&out);
                XFlush(dpy);
            } else if (ev.type == SelectionClear) {
                stop = 1;
            }
        }
        { struct timespec ts = {0, 1000000L}; (void)nanosleep(&ts, NULL); }
    }
    printf("text_requests=%lu\n", text_requests);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}

static int
paste(void)
{
    Display *dpy = XOpenDisplay(NULL);
    Window win;
    Atom clipboard, utf8, property;
    XEvent ev;
    if (dpy == NULL) return 2;
    win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
    clipboard = XInternAtom(dpy, "CLIPBOARD", False);
    utf8 = XInternAtom(dpy, "UTF8_STRING", False);
    property = XInternAtom(dpy, "_RAWCLIP_TEST_PASTE", False);
    XConvertSelection(dpy, clipboard, utf8, property, win, CurrentTime);
    XFlush(dpy);
    for (;;) {
        XNextEvent(dpy, &ev);
        if (ev.type == SelectionNotify) break;
    }
    if (ev.xselection.property == None) return 3;
    {
        Atom actual;
        int format;
        unsigned long nitems, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(dpy, win, property, 0, 32768, True, AnyPropertyType,
                               &actual, &format, &nitems, &after, &data) != Success ||
            data == NULL || format != 8) return 4;
        fwrite(data, 1U, nitems, stdout);
        XFree(data);
    }
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
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
    if (argc >= 2 && strcmp(argv[1], "paste") == 0 && argc == 2) return paste();
    if (argc >= 3 && strcmp(argv[1], "offer") == 0) {
        bool secret = false;
        const char *class_name = NULL;
        int i;
        for (i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--secret") == 0) secret = true;
            else if (strcmp(argv[i], "--class") == 0 && i + 1 < argc) class_name = argv[++i];
            else return 2;
        }
        return offer(argv[2], secret, class_name);
    }
    return 2;
}
