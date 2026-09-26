#include "rawclip.h"
#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static void
entry_destroy(struct rawclip_entry *e)
{
    if (e == NULL) {
        return;
    }
    if (e->text != NULL) {
        memset(e->text, 0, e->len);
        free(e->text);
    }
    memset(e, 0, sizeof(*e));
}

void
rawclip_history_init(struct rawclip_history *h, size_t max_entries,
                     size_t max_entry_bytes, bool deduplicate)
{
    memset(h, 0, sizeof(*h));
    h->max_entries = max_entries;
    h->max_entry_bytes = max_entry_bytes;
    h->deduplicate = deduplicate;
    h->next_id = 1U;
}

void
rawclip_history_destroy(struct rawclip_history *h)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        entry_destroy(&h->entries[i]);
    }
    free(h->entries);
    memset(h, 0, sizeof(*h));
}

static int
reserve_one(struct rawclip_history *h)
{
    struct rawclip_entry *p;
    size_t cap;

    if (h->len < h->cap) {
        return 0;
    }
    cap = h->cap == 0U ? 16U : h->cap * 2U;
    if (cap > h->max_entries) {
        cap = h->max_entries;
    }
    if (cap <= h->cap) {
        errno = ENOSPC;
        return -1;
    }
    p = realloc(h->entries, cap * sizeof(*p));
    if (p == NULL) {
        return -1;
    }
    h->entries = p;
    h->cap = cap;
    return 0;
}

static void
evict_oldest(struct rawclip_history *h)
{
    if (h->len == 0U) {
        return;
    }
    entry_destroy(&h->entries[h->len - 1U]);
    h->len--;
}

static ssize_t
find_duplicate(const struct rawclip_history *h, const char *text, size_t len)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        if (h->entries[i].len == len && memcmp(h->entries[i].text, text, len) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

int
rawclip_history_add(struct rawclip_history *h, const char *text, size_t len,
                    uint64_t timestamp_ms, uint64_t *id_out)
{
    struct rawclip_entry e;
    ssize_t duplicate;

    if (h == NULL || text == NULL || len == 0U || len > h->max_entry_bytes ||
        h->max_entries == 0U || memchr(text, '\0', len) != NULL) {
        errno = EINVAL;
        return -1;
    }

    duplicate = h->deduplicate ? find_duplicate(h, text, len) : -1;
    if (duplicate >= 0) {
        e = h->entries[(size_t)duplicate];
        if ((size_t)duplicate > 0U) {
            memmove(&h->entries[1], &h->entries[0], (size_t)duplicate * sizeof(e));
        }
        e.timestamp_ms = timestamp_ms;
        h->entries[0] = e;
        if (id_out != NULL) {
            *id_out = e.id;
        }
        return 0;
    }

    while (h->len >= h->max_entries) {
        evict_oldest(h);
    }
    if (reserve_one(h) != 0) {
        return -1;
    }

    memset(&e, 0, sizeof(e));
    e.text = rawclip_xstrndup(text, len);
    if (e.text == NULL) {
        return -1;
    }
    e.len = len;
    e.timestamp_ms = timestamp_ms;
    e.id = h->next_id++;
    if (h->next_id == 0U) {
        h->next_id = 1U;
    }

    if (h->len > 0U) {
        memmove(&h->entries[1], &h->entries[0], h->len * sizeof(e));
    }
    h->entries[0] = e;
    h->len++;
    if (id_out != NULL) {
        *id_out = e.id;
    }
    return 0;
}

const struct rawclip_entry *
rawclip_history_get(const struct rawclip_history *h, uint64_t id)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        if (h->entries[i].id == id) {
            return &h->entries[i];
        }
    }
    return NULL;
}

int
rawclip_history_delete(struct rawclip_history *h, uint64_t id)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        if (h->entries[i].id == id) {
            entry_destroy(&h->entries[i]);
            if (i + 1U < h->len) {
                memmove(&h->entries[i], &h->entries[i + 1U],
                        (h->len - i - 1U) * sizeof(h->entries[0]));
            }
            h->len--;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

void
rawclip_history_clear(struct rawclip_history *h)
{
    size_t i;
    for (i = 0; i < h->len; i++) {
        entry_destroy(&h->entries[i]);
    }
    h->len = 0U;
}
