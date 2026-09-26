#define _POSIX_C_SOURCE 200809L

#include "x11.h"
#include "util.h"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>

#include <errno.h>
#include <fnmatch.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct owner_meta {
    char *instance;
    char *class_name;
    char *title;
    char *executable;
};

enum receive_stage {
    RECEIVE_IDLE,
    RECEIVE_TARGETS,
    RECEIVE_TEXT,
    RECEIVE_INCR,
};

struct rawclip_x11 {
    Display *dpy;
    Window win;
    int xfixes_event_base;
    Atom clipboard;
    Atom primary;
    Atom targets;
    Atom timestamp;
    Atom utf8;
    Atom string;
    Atom incr;
    Atom sensitive;
    Atom property;
    Atom time_property;
    Atom net_wm_name;
    Atom net_wm_pid;
    const struct rawclip_config *cfg;
    rawclip_capture_fn capture;
    void *userdata;
    bool paused;
    Atom pending_selection;
    Atom pending_target;
    Window pending_owner;
    enum receive_stage stage;
    unsigned char *buf;
    size_t buf_len;
    size_t buf_cap;
    size_t max_direct_bytes;
    size_t server_direct_bytes;
    char *owned_text;
    size_t owned_len;
    Time owned_time;
};

static void
set_error(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;
    if (err == NULL || size == 0U) return;
    va_start(ap, fmt);
    (void)vsnprintf(err, size, fmt, ap);
    va_end(ap);
}

static void
owner_meta_destroy(struct owner_meta *m)
{
    free(m->instance);
    free(m->class_name);
    free(m->title);
    free(m->executable);
    memset(m, 0, sizeof(*m));
}

static char *
window_string_property(Display *dpy, Window w, Atom property)
{
    Atom actual;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    char *out = NULL;

    if (XGetWindowProperty(dpy, w, property, 0, 1024, False, AnyPropertyType,
                           &actual, &format, &nitems, &after, &data) != Success ||
        data == NULL || format != 8 || nitems == 0U) {
        if (data != NULL) XFree(data);
        return NULL;
    }
    out = rawclip_xstrndup((const char *)data, (size_t)nitems);
    XFree(data);
    return out;
}

static int
window_pid(Display *dpy, Window w, Atom net_wm_pid, unsigned long *pid)
{
    Atom actual;
    int format;
    unsigned long nitems, after;
    unsigned char *data = NULL;
    int rc = -1;
    if (XGetWindowProperty(dpy, w, net_wm_pid, 0, 1, False, XA_CARDINAL,
                           &actual, &format, &nitems, &after, &data) == Success &&
        data != NULL && actual == XA_CARDINAL && format == 32 && nitems == 1U) {
        *pid = *(unsigned long *)data;
        rc = 0;
    }
    if (data != NULL) XFree(data);
    return rc;
}

static char *
proc_executable(unsigned long pid)
{
    char path[64];
    char target[PATH_MAX];
    ssize_t n;
    if (snprintf(path, sizeof(path), "/proc/%lu/exe", pid) >= (int)sizeof(path)) return NULL;
    n = readlink(path, target, sizeof(target) - 1U);
    if (n < 0) return NULL;
    target[n] = '\0';
    return rawclip_xstrndup(target, (size_t)n);
}

static void
collect_owner_meta(struct rawclip_x11 *x, Window owner, struct owner_meta *m)
{
    Window current = owner;
    Window root = DefaultRootWindow(x->dpy);
    int depth;
    memset(m, 0, sizeof(*m));

    for (depth = 0; current != None && current != root && depth < 16; depth++) {
        XClassHint hint;
        Window parent, query_root, *children = NULL;
        unsigned int nchildren = 0U;
        unsigned long pid;

        memset(&hint, 0, sizeof(hint));
        if ((m->instance == NULL || m->class_name == NULL) && XGetClassHint(x->dpy, current, &hint)) {
            if (m->instance == NULL && hint.res_name != NULL) m->instance = rawclip_xstrndup(hint.res_name, strlen(hint.res_name));
            if (m->class_name == NULL && hint.res_class != NULL) m->class_name = rawclip_xstrndup(hint.res_class, strlen(hint.res_class));
            if (hint.res_name != NULL) XFree(hint.res_name);
            if (hint.res_class != NULL) XFree(hint.res_class);
        }
        if (m->title == NULL) {
            m->title = window_string_property(x->dpy, current, x->net_wm_name);
            if (m->title == NULL) {
                char *name = NULL;
                if (XFetchName(x->dpy, current, &name) && name != NULL) {
                    m->title = rawclip_xstrndup(name, strlen(name));
                    XFree(name);
                }
            }
        }
        if (m->executable == NULL && window_pid(x->dpy, current, x->net_wm_pid, &pid) == 0) {
            m->executable = proc_executable(pid);
        }
        if (!XQueryTree(x->dpy, current, &query_root, &parent, &children, &nchildren)) break;
        if (children != NULL) XFree(children);
        if (parent == current) break;
        current = parent;
    }
}

static bool
field_match(const char *pattern, const char *value)
{
    if (pattern == NULL) return true;
    if (value == NULL) return false;
    return fnmatch(pattern, value, 0) == 0;
}

static bool
owner_excluded(struct rawclip_x11 *x, Window owner)
{
    struct owner_meta m;
    size_t i;
    bool excluded = false;
    if (x->cfg->rules_len == 0U) return false;
    collect_owner_meta(x, owner, &m);
    for (i = 0U; i < x->cfg->rules_len; i++) {
        const struct rawclip_exclude_rule *r = &x->cfg->rules[i];
        if (field_match(r->instance, m.instance) &&
            field_match(r->class_name, m.class_name) &&
            field_match(r->title, m.title) &&
            field_match(r->executable, m.executable)) {
            excluded = true;
            break;
        }
    }
    owner_meta_destroy(&m);
    return excluded;
}

static void
reset_receive(struct rawclip_x11 *x)
{
    free(x->buf);
    x->buf = NULL;
    x->buf_len = 0U;
    x->buf_cap = 0U;
    x->stage = RECEIVE_IDLE;
    x->pending_selection = None;
    x->pending_target = None;
    x->pending_owner = None;
    XDeleteProperty(x->dpy, x->win, x->property);
}

static int
append_receive(struct rawclip_x11 *x, const unsigned char *data, size_t len)
{
    unsigned char *p;
    size_t cap;
    if (len == 0U) return 0;
    if (x->buf_len > x->cfg->max_entry_bytes || len > x->cfg->max_entry_bytes - x->buf_len ||
        x->buf_len + len > x->max_direct_bytes) {
        errno = EMSGSIZE;
        return -1;
    }
    if (x->buf_len + len > x->buf_cap) {
        cap = x->buf_cap == 0U ? 4096U : x->buf_cap;
        while (cap < x->buf_len + len) cap *= 2U;
        if (cap > x->cfg->max_entry_bytes) cap = x->cfg->max_entry_bytes;
        p = realloc(x->buf, cap + 1U);
        if (p == NULL) return -1;
        x->buf = p;
        x->buf_cap = cap;
    }
    memcpy(x->buf + x->buf_len, data, len);
    x->buf_len += len;
    x->buf[x->buf_len] = '\0';
    return 0;
}

static int
finish_text(struct rawclip_x11 *x)
{
    int rc = 0;
    if (x->buf_len > 0U && memchr(x->buf, '\0', x->buf_len) == NULL) {
        rc = x->capture(x->userdata, (const char *)x->buf, x->buf_len, rawclip_now_ms());
    }
    reset_receive(x);
    return rc;
}

static bool
target_list_contains(const Atom *atoms, unsigned long n, Atom target)
{
    unsigned long i;
    for (i = 0U; i < n; i++) if (atoms[i] == target) return true;
    return false;
}

static int
request_text(struct rawclip_x11 *x, const Atom *atoms, unsigned long n)
{
    Atom target = None;
    if (target_list_contains(atoms, n, x->utf8)) target = x->utf8;
    else if (target_list_contains(atoms, n, x->string)) target = x->string;
    if (target == None) {
        reset_receive(x);
        return 0;
    }
    x->stage = RECEIVE_TEXT;
    x->pending_target = target;
    XConvertSelection(x->dpy, x->pending_selection, target, x->property, x->win, CurrentTime);
    XFlush(x->dpy);
    return 0;
}

static int
selection_notify(struct rawclip_x11 *x, const XSelectionEvent *ev)
{
    Atom actual = None;
    int format = 0;
    unsigned long nitems = 0U, after = 0U;
    unsigned char *data = NULL;
    int rc = 0;

    if (ev->requestor != x->win || ev->selection != x->pending_selection || ev->property == None) {
        if (ev->requestor == x->win && ev->selection == x->pending_selection) reset_receive(x);
        return 0;
    }
    if (XGetWindowProperty(x->dpy, x->win, x->property, 0, (long)((x->cfg->max_entry_bytes + 3U) / 4U + 16U), True,
                           AnyPropertyType, &actual, &format, &nitems, &after, &data) != Success) {
        reset_receive(x);
        return -1;
    }
    if (x->stage == RECEIVE_TARGETS) {
        if (actual == XA_ATOM && format == 32 && data != NULL) {
            const Atom *atoms = (const Atom *)data;
            if (x->cfg->honor_password_manager_hint &&
                target_list_contains(atoms, nitems, x->sensitive)) {
                reset_receive(x);
            } else {
                rc = request_text(x, atoms, nitems);
            }
        } else {
            reset_receive(x);
        }
    } else if (x->stage == RECEIVE_TEXT) {
        if (actual == x->incr) {
            x->stage = RECEIVE_INCR;
            XDeleteProperty(x->dpy, x->win, x->property);
            XFlush(x->dpy);
        } else if (format == 8 && data != NULL) {
            if (actual == x->string) {
                char *utf8 = NULL;
                size_t utf8_len = 0U;
                if (rawclip_latin1_to_utf8(data, (size_t)nitems, &utf8, &utf8_len) == 0 &&
                    utf8_len <= x->cfg->max_entry_bytes && utf8_len <= x->max_direct_bytes &&
                    memchr(utf8, '\0', utf8_len) == NULL &&
                    append_receive(x, (const unsigned char *)utf8, utf8_len) == 0) {
                    rc = finish_text(x);
                } else {
                    reset_receive(x);
                }
                free(utf8);
            } else if (actual == x->utf8 && rawclip_utf8_valid(data, (size_t)nitems) &&
                       (size_t)nitems <= x->cfg->max_entry_bytes && (size_t)nitems <= x->max_direct_bytes &&
                       append_receive(x, data, (size_t)nitems) == 0) {
                rc = finish_text(x);
            } else {
                reset_receive(x);
            }
        } else {
            reset_receive(x);
        }
    }
    if (data != NULL) XFree(data);
    return rc;
}

static int
property_notify(struct rawclip_x11 *x, const XPropertyEvent *ev)
{
    Atom actual = None;
    int format = 0;
    unsigned long nitems = 0U, after = 0U;
    unsigned char *data = NULL;
    int rc = 0;
    if (x->stage != RECEIVE_INCR || ev->window != x->win || ev->atom != x->property || ev->state != PropertyNewValue) return 0;
    if (XGetWindowProperty(x->dpy, x->win, x->property, 0, (long)((x->cfg->max_entry_bytes + 3U) / 4U + 16U), True,
                           AnyPropertyType, &actual, &format, &nitems, &after, &data) != Success) {
        reset_receive(x);
        return -1;
    }
    if (nitems == 0U) {
        rc = finish_text(x);
    } else if (format != 8 || data == NULL) {
        reset_receive(x);
    } else if (x->pending_target == x->string) {
        char *utf8 = NULL;
        size_t utf8_len = 0U;
        if (rawclip_latin1_to_utf8(data, (size_t)nitems, &utf8, &utf8_len) != 0 ||
            append_receive(x, (const unsigned char *)utf8, utf8_len) != 0) {
            reset_receive(x);
        }
        free(utf8);
    } else if (!rawclip_utf8_valid(data, (size_t)nitems) || append_receive(x, data, (size_t)nitems) != 0) {
        reset_receive(x);
    }
    if (data != NULL) XFree(data);
    return rc;
}

static Time
server_time(struct rawclip_x11 *x)
{
    XEvent ev;
    unsigned char value = 0;
    XChangeProperty(x->dpy, x->win, x->time_property, XA_INTEGER, 8, PropModeReplace, &value, 1);
    XFlush(x->dpy);
    for (;;) {
        XWindowEvent(x->dpy, x->win, PropertyChangeMask, &ev);
        if (ev.type == PropertyNotify && ev.xproperty.atom == x->time_property) return ev.xproperty.time;
    }
}

static void
selection_request(struct rawclip_x11 *x, const XSelectionRequestEvent *req)
{
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

    if (req->selection == x->clipboard && x->owned_text != NULL) {
        if (req->target == x->targets) {
            Atom list[] = {x->targets, x->timestamp, x->utf8, x->string};
            XChangeProperty(x->dpy, req->requestor, property, XA_ATOM, 32,
                            PropModeReplace, (unsigned char *)list, 4);
            ok = true;
        } else if (req->target == x->timestamp) {
            unsigned long t = x->owned_time;
            XChangeProperty(x->dpy, req->requestor, property, XA_INTEGER, 32,
                            PropModeReplace, (unsigned char *)&t, 1);
            ok = true;
        } else if (req->target == x->utf8 && x->owned_len <= x->max_direct_bytes) {
            XChangeProperty(x->dpy, req->requestor, property, x->utf8, 8,
                            PropModeReplace, (unsigned char *)x->owned_text, (int)x->owned_len);
            ok = true;
        } else if (req->target == x->string && x->owned_len <= x->max_direct_bytes) {
            unsigned char *latin1 = NULL;
            size_t latin1_len = 0U;
            if (rawclip_utf8_to_latin1(x->owned_text, x->owned_len, &latin1, &latin1_len) == 0) {
                XChangeProperty(x->dpy, req->requestor, property, x->string, 8,
                                PropModeReplace, latin1, (int)latin1_len);
                free(latin1);
                ok = true;
            }
        }
    }
    if (ok) out.property = property;
    XSendEvent(x->dpy, req->requestor, False, 0, (XEvent *)&out);
    XFlush(x->dpy);
}

static void
owner_changed(struct rawclip_x11 *x, const XFixesSelectionNotifyEvent *ev)
{
    if (x->paused || ev->owner == None || ev->owner == x->win) return;
    if ((ev->selection == x->clipboard && !x->cfg->monitor_clipboard) ||
        (ev->selection == x->primary && !x->cfg->monitor_primary)) return;
    reset_receive(x);
    if (owner_excluded(x, ev->owner)) return;
    x->pending_selection = ev->selection;
    x->pending_owner = ev->owner;
    x->stage = RECEIVE_TARGETS;
    XConvertSelection(x->dpy, ev->selection, x->targets, x->property, x->win, ev->timestamp);
    XFlush(x->dpy);
}

struct rawclip_x11 *
rawclip_x11_create(const struct rawclip_config *cfg, rawclip_capture_fn capture,
                   void *userdata, char *err, size_t err_size)
{
    struct rawclip_x11 *x;
    int error_base;
    long max_request;
    x = calloc(1, sizeof(*x));
    if (x == NULL) return NULL;
    x->dpy = XOpenDisplay(NULL);
    if (x->dpy == NULL) {
        set_error(err, err_size, "cannot open X display");
        free(x);
        errno = ENODEV;
        return NULL;
    }
    if (!XFixesQueryExtension(x->dpy, &x->xfixes_event_base, &error_base)) {
        set_error(err, err_size, "XFixes extension is unavailable");
        XCloseDisplay(x->dpy);
        free(x);
        errno = ENOTSUP;
        return NULL;
    }
    x->win = XCreateSimpleWindow(x->dpy, DefaultRootWindow(x->dpy), -10, -10, 1, 1, 0, 0, 0);
    XSelectInput(x->dpy, x->win, PropertyChangeMask);
    x->clipboard = XInternAtom(x->dpy, "CLIPBOARD", False);
    x->primary = XA_PRIMARY;
    x->targets = XInternAtom(x->dpy, "TARGETS", False);
    x->timestamp = XInternAtom(x->dpy, "TIMESTAMP", False);
    x->utf8 = XInternAtom(x->dpy, "UTF8_STRING", False);
    x->string = XA_STRING;
    x->incr = XInternAtom(x->dpy, "INCR", False);
    x->sensitive = XInternAtom(x->dpy, "x-kde-passwordManagerHint", False);
    x->property = XInternAtom(x->dpy, "_RAWCLIP_TRANSFER", False);
    x->time_property = XInternAtom(x->dpy, "_RAWCLIP_SERVER_TIME", False);
    x->net_wm_name = XInternAtom(x->dpy, "_NET_WM_NAME", False);
    x->net_wm_pid = XInternAtom(x->dpy, "_NET_WM_PID", False);
    x->cfg = cfg;
    x->capture = capture;
    x->userdata = userdata;
    max_request = XMaxRequestSize(x->dpy);
    x->server_direct_bytes = max_request > 1024 ? ((size_t)max_request - 256U) * 4U : 65536U;
    x->max_direct_bytes = x->server_direct_bytes < cfg->max_entry_bytes ? x->server_direct_bytes : cfg->max_entry_bytes;
    XFixesSelectSelectionInput(x->dpy, x->win, x->clipboard,
                               XFixesSetSelectionOwnerNotifyMask |
                               XFixesSelectionWindowDestroyNotifyMask |
                               XFixesSelectionClientCloseNotifyMask);
    XFixesSelectSelectionInput(x->dpy, x->win, x->primary,
                               XFixesSetSelectionOwnerNotifyMask |
                               XFixesSelectionWindowDestroyNotifyMask |
                               XFixesSelectionClientCloseNotifyMask);
    XFlush(x->dpy);
    return x;
}

void
rawclip_x11_destroy(struct rawclip_x11 *x)
{
    if (x == NULL) return;
    reset_receive(x);
    free(x->owned_text);
    if (x->dpy != NULL) {
        if (x->win != None) XDestroyWindow(x->dpy, x->win);
        XCloseDisplay(x->dpy);
    }
    free(x);
}

int
rawclip_x11_fd(const struct rawclip_x11 *x)
{
    return ConnectionNumber(x->dpy);
}

int
rawclip_x11_dispatch(struct rawclip_x11 *x)
{
    while (XPending(x->dpy) > 0) {
        XEvent ev;
        XNextEvent(x->dpy, &ev);
        if (ev.type == x->xfixes_event_base + XFixesSelectionNotify) {
            owner_changed(x, (const XFixesSelectionNotifyEvent *)&ev);
        } else if (ev.type == SelectionNotify) {
            if (selection_notify(x, &ev.xselection) != 0) return -1;
        } else if (ev.type == PropertyNotify) {
            if (property_notify(x, &ev.xproperty) != 0) return -1;
        } else if (ev.type == SelectionRequest) {
            selection_request(x, &ev.xselectionrequest);
        } else if (ev.type == SelectionClear && ev.xselectionclear.selection == x->clipboard) {
            free(x->owned_text);
            x->owned_text = NULL;
            x->owned_len = 0U;
        }
    }
    return 0;
}

void
rawclip_x11_set_paused(struct rawclip_x11 *x, bool paused)
{
    x->paused = paused;
    if (paused) reset_receive(x);
}

void
rawclip_x11_set_config(struct rawclip_x11 *x, const struct rawclip_config *cfg)
{
    x->cfg = cfg;
    x->max_direct_bytes = x->server_direct_bytes < cfg->max_entry_bytes ? x->server_direct_bytes : cfg->max_entry_bytes;
}

int
rawclip_x11_own_clipboard(struct rawclip_x11 *x, const char *text, size_t len)
{
    char *copy;
    Time t;
    if (text == NULL || len == 0U || len > x->cfg->max_entry_bytes || len > x->max_direct_bytes ||
        memchr(text, '\0', len) != NULL) {
        errno = EINVAL;
        return -1;
    }
    copy = rawclip_xstrndup(text, len);
    if (copy == NULL) return -1;
    reset_receive(x);
    t = server_time(x);
    XSetSelectionOwner(x->dpy, x->clipboard, x->win, t);
    XSync(x->dpy, False);
    if (XGetSelectionOwner(x->dpy, x->clipboard) != x->win) {
        free(copy);
        errno = EBUSY;
        return -1;
    }
    free(x->owned_text);
    x->owned_text = copy;
    x->owned_len = len;
    x->owned_time = t;
    return 0;
}

void
rawclip_x11_clear_owned(struct rawclip_x11 *x)
{
    if (XGetSelectionOwner(x->dpy, x->clipboard) == x->win) {
        XSetSelectionOwner(x->dpy, x->clipboard, None, CurrentTime);
        XFlush(x->dpy);
    }
    free(x->owned_text);
    x->owned_text = NULL;
    x->owned_len = 0U;
}
