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

#define RAWCLIP_MAX_SEND_TRANSFERS 16U
#define RAWCLIP_SEND_TIMEOUT_MS 10000ULL

struct send_transfer {
    Window requestor;
    Atom selection;
    Atom property;
    Atom target;
    unsigned char *data;
    size_t len;
    size_t offset;
    uint64_t activity_ms;
    struct send_transfer *next;
};

struct owned_selection {
    char *text;
    size_t len;
    Time time;
    bool synchronized;
};

struct owner_meta {
    char *instance;
    char *class_name;
    char *title;
    char *executable;
};

enum receive_stage {
    RECEIVE_IDLE,
    RECEIVE_TARGETS,
    RECEIVE_HINT,
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
    Atom pending_text_target;
    Window pending_owner;
    enum receive_stage stage;
    unsigned char *buf;
    size_t buf_len;
    size_t buf_cap;
    size_t server_direct_bytes;
    struct send_transfer *send_head;
    size_t send_len;
    struct owned_selection owned_clipboard;
    struct owned_selection owned_primary;
};

static int own_selection(struct rawclip_x11 *x, Atom selection,
                         const char *text, size_t len, bool synchronized);

static int
x_error_handler(Display *dpy, XErrorEvent *event)
{
    (void)dpy;
    (void)event;
    return 0;
}

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
    x->pending_text_target = None;
    x->pending_owner = None;
    XDeleteProperty(x->dpy, x->win, x->property);
}

static struct owned_selection *
owned_slot(struct rawclip_x11 *x, Atom selection)
{
    if (selection == x->clipboard) return &x->owned_clipboard;
    if (selection == x->primary) return &x->owned_primary;
    return NULL;
}

static Atom
sync_destination(const struct rawclip_x11 *x, Atom source)
{
    if (source == x->primary &&
        (x->cfg->sync_mode == RAWCLIP_SYNC_PRIMARY_TO_CLIPBOARD ||
         x->cfg->sync_mode == RAWCLIP_SYNC_BIDIRECTIONAL))
        return x->clipboard;
    if (source == x->clipboard &&
        (x->cfg->sync_mode == RAWCLIP_SYNC_CLIPBOARD_TO_PRIMARY ||
         x->cfg->sync_mode == RAWCLIP_SYNC_BIDIRECTIONAL))
        return x->primary;
    return None;
}

static bool
sync_targets_selection(const struct rawclip_config *cfg, Atom selection,
                       Atom clipboard, Atom primary)
{
    if (selection == clipboard)
        return cfg->sync_mode == RAWCLIP_SYNC_PRIMARY_TO_CLIPBOARD ||
               cfg->sync_mode == RAWCLIP_SYNC_BIDIRECTIONAL;
    if (selection == primary)
        return cfg->sync_mode == RAWCLIP_SYNC_CLIPBOARD_TO_PRIMARY ||
               cfg->sync_mode == RAWCLIP_SYNC_BIDIRECTIONAL;
    return false;
}

static void
owned_selection_clear(struct owned_selection *owned)
{
    if (owned->text != NULL) {
        rawclip_memzero(owned->text, owned->len);
        free(owned->text);
    }
    memset(owned, 0, sizeof(*owned));
}

static bool
owned_selection_matches(const struct rawclip_x11 *x, Atom selection,
                        const char *text, size_t len)
{
    const struct owned_selection *owned;
    if (selection == x->clipboard) owned = &x->owned_clipboard;
    else if (selection == x->primary) owned = &x->owned_primary;
    else return false;
    return XGetSelectionOwner(x->dpy, selection) == x->win &&
           owned->text != NULL && owned->len == len &&
           memcmp(owned->text, text, len) == 0;
}

static void
send_transfer_destroy(struct send_transfer *t)
{
    if (t == NULL) return;
    if (t->data != NULL) {
        rawclip_memzero(t->data, t->len);
        free(t->data);
    }
    free(t);
}

static bool
requestor_has_transfer(const struct rawclip_x11 *x, Window requestor)
{
    const struct send_transfer *t;
    for (t = x->send_head; t != NULL; t = t->next) {
        if (t->requestor == requestor) return true;
    }
    return false;
}

static void
remove_send_link(struct rawclip_x11 *x, struct send_transfer **link)
{
    struct send_transfer *t = *link;
    Window requestor;
    if (t == NULL) return;
    requestor = t->requestor;
    *link = t->next;
    if (x->send_len > 0U) x->send_len--;
    send_transfer_destroy(t);
    if (requestor != x->win && !requestor_has_transfer(x, requestor)) {
        XSelectInput(x->dpy, requestor, NoEventMask);
    }
}

static void
cancel_send_for_window(struct rawclip_x11 *x, Window requestor)
{
    struct send_transfer **link = &x->send_head;
    while (*link != NULL) {
        if ((*link)->requestor == requestor) remove_send_link(x, link);
        else link = &(*link)->next;
    }
}

static void
cancel_send_for_selection(struct rawclip_x11 *x, Atom selection)
{
    struct send_transfer **link = &x->send_head;
    while (*link != NULL) {
        if ((*link)->selection == selection) remove_send_link(x, link);
        else link = &(*link)->next;
    }
}

static void
cancel_send_all(struct rawclip_x11 *x)
{
    while (x->send_head != NULL) remove_send_link(x, &x->send_head);
}

static void
prune_send_transfers(struct rawclip_x11 *x)
{
    struct send_transfer **link = &x->send_head;
    uint64_t now = rawclip_now_ms();
    while (*link != NULL) {
        struct send_transfer *t = *link;
        XWindowAttributes attr;
        bool expired = now >= t->activity_ms && now - t->activity_ms > RAWCLIP_SEND_TIMEOUT_MS;
        bool gone = XGetWindowAttributes(x->dpy, t->requestor, &attr) == 0;
        if (expired || gone) remove_send_link(x, link);
        else link = &t->next;
    }
}

static bool
start_send_transfer(struct rawclip_x11 *x, Atom selection, Window requestor,
                    Atom property, Atom target, const unsigned char *data,
                    size_t len)
{
    struct send_transfer **link;
    struct send_transfer *t;
    XWindowAttributes attr;
    unsigned long total;

    if (requestor == None || requestor == x->win || property == None || data == NULL || len == 0U)
        return false;

    prune_send_transfers(x);
    link = &x->send_head;
    while (*link != NULL) {
        if ((*link)->requestor == requestor && (*link)->property == property) {
            remove_send_link(x, link);
            break;
        }
        link = &(*link)->next;
    }
    if (x->send_len >= RAWCLIP_MAX_SEND_TRANSFERS ||
        XGetWindowAttributes(x->dpy, requestor, &attr) == 0) return false;

    t = calloc(1, sizeof(*t));
    if (t == NULL) return false;
    t->data = malloc(len);
    if (t->data == NULL) {
        free(t);
        return false;
    }
    memcpy(t->data, data, len);
    t->requestor = requestor;
    t->selection = selection;
    t->property = property;
    t->target = target;
    t->len = len;
    t->activity_ms = rawclip_now_ms();
    t->next = x->send_head;
    x->send_head = t;
    x->send_len++;

    XSelectInput(x->dpy, requestor, PropertyChangeMask | StructureNotifyMask);
    total = (unsigned long)len;
    XChangeProperty(x->dpy, requestor, property, x->incr, 32, PropModeReplace,
                    (unsigned char *)&total, 1);
    return true;
}

static bool
serve_selection_data(struct rawclip_x11 *x, Atom selection, Window requestor,
                     Atom property, Atom target, const unsigned char *data,
                     size_t len)
{
    if (len <= x->server_direct_bytes) {
        XChangeProperty(x->dpy, requestor, property, target, 8, PropModeReplace,
                        data, (int)len);
        return true;
    }
    return start_send_transfer(x, selection, requestor, property, target, data, len);
}

static int
outgoing_property_delete(struct rawclip_x11 *x, const XPropertyEvent *ev)
{
    struct send_transfer **link = &x->send_head;
    while (*link != NULL) {
        struct send_transfer *t = *link;
        if (t->requestor == ev->window && t->property == ev->atom) {
            if (t->offset < t->len) {
                size_t left = t->len - t->offset;
                size_t chunk = left < x->server_direct_bytes ? left : x->server_direct_bytes;
                XChangeProperty(x->dpy, t->requestor, t->property, t->target, 8,
                                PropModeReplace, t->data + t->offset, (int)chunk);
                t->offset += chunk;
                t->activity_ms = rawclip_now_ms();
                XFlush(x->dpy);
            } else {
                XChangeProperty(x->dpy, t->requestor, t->property, t->target, 8,
                                PropModeReplace, (const unsigned char *)"", 0);
                XFlush(x->dpy);
                remove_send_link(x, link);
            }
            return 1;
        }
        link = &t->next;
    }
    return 0;
}

static int
append_receive(struct rawclip_x11 *x, const unsigned char *data, size_t len)
{
    unsigned char *p;
    size_t cap;
    if (len == 0U) return 0;
    if (x->buf_len > x->cfg->max_entry_bytes || len > x->cfg->max_entry_bytes - x->buf_len) {
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
    Atom source = x->pending_selection;
    bool valid = x->buf_len > 0U && memchr(x->buf, '\0', x->buf_len) == NULL;
    if (valid && x->pending_target == x->utf8) {
        valid = rawclip_utf8_valid(x->buf, x->buf_len);
    }
    if (valid) {
        Atom destination;
        rc = x->capture(x->userdata, (const char *)x->buf, x->buf_len,
                        rawclip_now_ms());
        destination = sync_destination(x, source);
        if (rc == RAWCLIP_CAPTURE_ADMITTED && destination != None) {
            /* Selection ownership is inherently racy. History admission remains
             * authoritative even if another client wins the destination race. */
            (void)own_selection(x, destination, (const char *)x->buf, x->buf_len, true);
        }
    }
    reset_receive(x);
    return rc == RAWCLIP_CAPTURE_FATAL ? -1 : 0;
}

static bool
target_list_contains(const Atom *atoms, unsigned long n, Atom target)
{
    unsigned long i;
    for (i = 0U; i < n; i++) if (atoms[i] == target) return true;
    return false;
}

static Atom
choose_text_target(struct rawclip_x11 *x, const Atom *atoms, unsigned long n)
{
    if (target_list_contains(atoms, n, x->utf8)) return x->utf8;
    if (target_list_contains(atoms, n, x->string)) return x->string;
    return None;
}

static void
request_target(struct rawclip_x11 *x, enum receive_stage stage, Atom target)
{
    x->stage = stage;
    x->pending_target = target;
    XConvertSelection(x->dpy, x->pending_selection, target, x->property, x->win,
                      CurrentTime);
    XFlush(x->dpy);
}

static void
request_text(struct rawclip_x11 *x, Atom target)
{
    x->pending_text_target = None;
    request_target(x, RECEIVE_TEXT, target);
}

static void
request_hint(struct rawclip_x11 *x, Atom text_target)
{
    x->pending_text_target = text_target;
    request_target(x, RECEIVE_HINT, x->sensitive);
}

static int
selection_notify(struct rawclip_x11 *x, const XSelectionEvent *ev)
{
    Atom actual = None;
    int format = 0;
    unsigned long nitems = 0U, after = 0U;
    unsigned char *data = NULL;
    int rc = 0;

    if (ev->requestor != x->win || ev->selection != x->pending_selection ||
        ev->target != x->pending_target || ev->property == None) {
        if (ev->requestor == x->win && ev->selection == x->pending_selection)
            reset_receive(x);
        return 0;
    }
    if (x->pending_owner == None ||
        XGetSelectionOwner(x->dpy, x->pending_selection) != x->pending_owner) {
        reset_receive(x);
        return 0;
    }
    if (XGetWindowProperty(x->dpy, x->win, x->property, 0,
                           x->stage == RECEIVE_HINT ? 16L :
                           (long)((x->cfg->max_entry_bytes + 3U) / 4U + 16U),
                           True, AnyPropertyType, &actual, &format, &nitems,
                           &after, &data) != Success) {
        reset_receive(x);
        return -1;
    }
    if (after != 0U) {
        reset_receive(x);
    } else if (x->stage == RECEIVE_TARGETS) {
        if (actual == XA_ATOM && format == 32 && data != NULL) {
            const Atom *atoms = (const Atom *)data;
            Atom text_target = choose_text_target(x, atoms, nitems);
            if (text_target == None) {
                reset_receive(x);
            } else if (x->cfg->honor_password_manager_hint &&
                       target_list_contains(atoms, nitems, x->sensitive)) {
                request_hint(x, text_target);
            } else {
                request_text(x, text_target);
            }
        } else {
            reset_receive(x);
        }
    } else if (x->stage == RECEIVE_HINT) {
        if (format != 8 || data == NULL || nitems == 0U ||
            memchr(data, '\0', (size_t)nitems) != NULL) {
            reset_receive(x);
        } else if (nitems == 6U && memcmp(data, "secret", 6U) == 0) {
            reset_receive(x);
        } else if (x->pending_text_target != None) {
            Atom text_target = x->pending_text_target;
            request_text(x, text_target);
        } else {
            reset_receive(x);
        }
    } else if (x->stage == RECEIVE_TEXT) {
        if (actual == x->incr) {
            if (format != 32 || data == NULL || nitems != 1U) {
                reset_receive(x);
            } else {
                x->stage = RECEIVE_INCR;
                XDeleteProperty(x->dpy, x->win, x->property);
                XFlush(x->dpy);
            }
        } else if (format == 8 && data != NULL) {
            if (actual == x->string) {
                char *utf8 = NULL;
                size_t utf8_len = 0U;
                if (rawclip_latin1_to_utf8(data, (size_t)nitems, &utf8, &utf8_len) == 0 &&
                    utf8_len <= x->cfg->max_entry_bytes &&
                    memchr(utf8, '\0', utf8_len) == NULL &&
                    append_receive(x, (const unsigned char *)utf8, utf8_len) == 0) {
                    rc = finish_text(x);
                } else {
                    reset_receive(x);
                }
                if (utf8 != NULL) rawclip_memzero(utf8, utf8_len);
                free(utf8);
            } else if (actual == x->utf8 && rawclip_utf8_valid(data, (size_t)nitems) &&
                       (size_t)nitems <= x->cfg->max_entry_bytes &&
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
    if (ev->state == PropertyDelete && outgoing_property_delete(x, ev) != 0) return 0;
    if (x->stage != RECEIVE_INCR || ev->window != x->win ||
        ev->atom != x->property || ev->state != PropertyNewValue) return 0;
    if (x->pending_owner == None ||
        XGetSelectionOwner(x->dpy, x->pending_selection) != x->pending_owner) {
        reset_receive(x);
        return 0;
    }
    if (XGetWindowProperty(x->dpy, x->win, x->property, 0,
                           (long)((x->cfg->max_entry_bytes + 3U) / 4U + 16U),
                           True, AnyPropertyType, &actual, &format, &nitems,
                           &after, &data) != Success) {
        reset_receive(x);
        return -1;
    }
    if (after != 0U) {
        XDeleteProperty(x->dpy, x->win, x->property);
        reset_receive(x);
    } else if (format != 8 || actual != x->pending_target ||
               (nitems > 0U && data == NULL)) {
        reset_receive(x);
    } else if (nitems == 0U) {
        rc = finish_text(x);
    } else if (x->pending_target == x->string) {
        char *utf8 = NULL;
        size_t utf8_len = 0U;
        if (rawclip_latin1_to_utf8(data, (size_t)nitems, &utf8, &utf8_len) != 0 ||
            append_receive(x, (const unsigned char *)utf8, utf8_len) != 0) {
            reset_receive(x);
        }
        if (utf8 != NULL) rawclip_memzero(utf8, utf8_len);
        free(utf8);
    } else if (append_receive(x, data, (size_t)nitems) != 0) {
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

static int
own_selection(struct rawclip_x11 *x, Atom selection, const char *text, size_t len,
              bool synchronized)
{
    struct owned_selection *owned = owned_slot(x, selection);
    char *copy;
    Time t;

    if (owned == NULL || text == NULL || len == 0U ||
        len > x->cfg->max_entry_bytes || memchr(text, '\0', len) != NULL) {
        errno = EINVAL;
        return -1;
    }
    if (owned_selection_matches(x, selection, text, len)) {
        if (!synchronized) owned->synchronized = false;
        return 0;
    }
    copy = rawclip_xstrndup(text, len);
    if (copy == NULL) return -1;
    t = server_time(x);
    XSetSelectionOwner(x->dpy, selection, x->win, t);
    XSync(x->dpy, False);
    if (XGetSelectionOwner(x->dpy, selection) != x->win) {
        rawclip_memzero(copy, len);
        free(copy);
        errno = EBUSY;
        return -1;
    }
    cancel_send_for_selection(x, selection);
    owned_selection_clear(owned);
    owned->text = copy;
    owned->len = len;
    owned->time = t;
    owned->synchronized = synchronized;
    return 0;
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

    {
        struct owned_selection *owned = owned_slot(x, req->selection);
        if (owned != NULL && owned->text != NULL &&
            XGetSelectionOwner(x->dpy, req->selection) == x->win) {
            if (req->target == x->targets) {
                Atom list[] = {x->targets, x->timestamp, x->utf8, x->string};
                XChangeProperty(x->dpy, req->requestor, property, XA_ATOM, 32,
                                PropModeReplace, (unsigned char *)list, 4);
                ok = true;
            } else if (req->target == x->timestamp) {
                unsigned long t = owned->time;
                XChangeProperty(x->dpy, req->requestor, property, XA_INTEGER, 32,
                                PropModeReplace, (unsigned char *)&t, 1);
                ok = true;
            } else if (req->target == x->utf8) {
                ok = serve_selection_data(x, req->selection, req->requestor,
                                          property, x->utf8,
                                          (const unsigned char *)owned->text,
                                          owned->len);
            } else if (req->target == x->string) {
                unsigned char *latin1 = NULL;
                size_t latin1_len = 0U;
                if (rawclip_utf8_to_latin1(owned->text, owned->len, &latin1,
                                           &latin1_len) == 0) {
                    ok = serve_selection_data(x, req->selection, req->requestor,
                                              property, x->string, latin1,
                                              latin1_len);
                    rawclip_memzero(latin1, latin1_len);
                    free(latin1);
                }
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
    if (x->paused) return;
    if ((ev->selection == x->clipboard && !x->cfg->monitor_clipboard) ||
        (ev->selection == x->primary && !x->cfg->monitor_primary)) return;
    if (ev->owner == None) {
        if (x->pending_selection == ev->selection) reset_receive(x);
        return;
    }
    if (ev->owner == x->win) return;
    reset_receive(x);
    if (owner_excluded(x, ev->owner)) return;
    x->pending_selection = ev->selection;
    x->pending_owner = ev->owner;
    x->stage = RECEIVE_TARGETS;
    x->pending_target = x->targets;
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
    XSetErrorHandler(x_error_handler);
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
    cancel_send_all(x);
    owned_selection_clear(&x->owned_clipboard);
    owned_selection_clear(&x->owned_primary);
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
        } else if (ev.type == DestroyNotify) {
            cancel_send_for_window(x, ev.xdestroywindow.window);
        } else if (ev.type == SelectionClear) {
            struct owned_selection *owned = owned_slot(x, ev.xselectionclear.selection);
            if (owned != NULL) {
                cancel_send_for_selection(x, ev.xselectionclear.selection);
                owned_selection_clear(owned);
            }
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
    Atom selections[2] = {x->clipboard, x->primary};
    size_t i;

    /* A pending receive was admitted under the old policy epoch. Do not let
     * its later replies cross an exclusion, sensitivity, monitor, or size
     * boundary that was replaced by this reload. */
    reset_receive(x);
    for (i = 0U; i < 2U; i++) {
        struct owned_selection *owned = owned_slot(x, selections[i]);
        if (owned != NULL && owned->synchronized &&
            !sync_targets_selection(cfg, selections[i], x->clipboard, x->primary)) {
            cancel_send_for_selection(x, selections[i]);
            if (XGetSelectionOwner(x->dpy, selections[i]) == x->win)
                XSetSelectionOwner(x->dpy, selections[i], None, CurrentTime);
            owned_selection_clear(owned);
        }
    }
    x->cfg = cfg;
    XFlush(x->dpy);
}

int
rawclip_x11_own_clipboard(struct rawclip_x11 *x, const char *text, size_t len)
{
    Atom destination;
    reset_receive(x);
    if (own_selection(x, x->clipboard, text, len, false) != 0) return -1;
    destination = sync_destination(x, x->clipboard);
    if (destination != None) (void)own_selection(x, destination, text, len, true);
    return 0;
}

void
rawclip_x11_clear_owned(struct rawclip_x11 *x)
{
    Atom selections[2] = {x->clipboard, x->primary};
    size_t i;
    cancel_send_all(x);
    for (i = 0U; i < 2U; i++) {
        struct owned_selection *owned = owned_slot(x, selections[i]);
        if (XGetSelectionOwner(x->dpy, selections[i]) == x->win)
            XSetSelectionOwner(x->dpy, selections[i], None, CurrentTime);
        if (owned != NULL) owned_selection_clear(owned);
    }
    XFlush(x->dpy);
}

void
rawclip_x11_get_status(struct rawclip_x11 *x, struct rawclip_x11_status *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (x == NULL) return;
    status->receive_pending = x->stage != RECEIVE_IDLE;
    status->outgoing_transfers = x->send_len;
    status->own_clipboard = XGetSelectionOwner(x->dpy, x->clipboard) == x->win;
    status->own_primary = XGetSelectionOwner(x->dpy, x->primary) == x->win;
}
