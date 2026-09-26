#include "rawclip.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

int
main(void)
{
    struct rawclip_history h;
    const struct rawclip_entry *e;
    uint64_t a, b, a2, c, d;
    char embedded[] = {'a', '\0', 'b'};

    rawclip_history_init(&h, 2U, 8U, true);
    assert(rawclip_history_add(&h, "alpha", 5U, 1U, &a) == 0);
    assert(rawclip_history_add(&h, "beta", 4U, 2U, &b) == 0);
    assert(h.len == 2U);
    assert(h.entries[0].id == b);
    assert(h.entries[1].id == a);

    assert(rawclip_history_set_pinned(&h, a, true) == 0);
    assert(rawclip_history_pinned_count(&h) == 1U);
    assert(rawclip_history_add(&h, "alpha", 5U, 3U, &a2) == 0);
    assert(a2 == a);
    assert(h.entries[0].id == a);
    assert(h.entries[0].timestamp_ms == 3U);
    assert(h.entries[0].pinned);

    assert(rawclip_history_add(&h, "gamma", 5U, 4U, &c) == 0);
    assert(h.len == 2U);
    assert(rawclip_history_get(&h, b) == NULL);
    e = rawclip_history_get(&h, a);
    assert(e != NULL && strcmp(e->text, "alpha") == 0 && e->pinned);

    assert(rawclip_history_set_pinned(&h, c, true) == 0);
    errno = 0;
    assert(rawclip_history_add(&h, "delta", 5U, 5U, &d) == -1);
    assert(errno == ENOSPC);
    assert(h.len == 2U);
    assert(rawclip_history_get(&h, a) != NULL);
    assert(rawclip_history_get(&h, c) != NULL);

    errno = 0;
    assert(rawclip_history_reconfigure(&h, 1U, 8U, true) == -1);
    assert(errno == ENOSPC);
    assert(h.max_entries == 2U);
    errno = 0;
    assert(rawclip_history_reconfigure(&h, 2U, 4U, true) == -1);
    assert(errno == EMSGSIZE);
    assert(h.max_entry_bytes == 8U);

    assert(rawclip_history_set_pinned(&h, c, false) == 0);
    assert(rawclip_history_reconfigure(&h, 1U, 8U, true) == 0);
    assert(h.len == 1U);
    assert(h.entries[0].id == a && h.entries[0].pinned);

    assert(rawclip_history_set_pinned(&h, a, false) == 0);
    assert(rawclip_history_add(&h, "beta", 4U, 6U, &b) == 0);
    assert(rawclip_history_set_pinned(&h, b, true) == 0);
    rawclip_history_clear_unpinned(&h);
    assert(h.len == 1U && h.entries[0].id == b && h.entries[0].pinned);

    errno = 0;
    assert(rawclip_history_add(&h, embedded, sizeof(embedded), 7U, NULL) == -1);
    assert(errno == EINVAL);
    assert(rawclip_history_add(&h, "oversized", 9U, 7U, NULL) == -1);

    assert(rawclip_history_delete(&h, b) == 0);
    assert(h.len == 0U);
    rawclip_history_clear(&h);
    rawclip_history_destroy(&h);
    return 0;
}
