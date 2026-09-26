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
    uint64_t a, b, a2;
    char embedded[] = {'a', '\0', 'b'};

    rawclip_history_init(&h, 2U, 8U, true);
    assert(rawclip_history_add(&h, "alpha", 5U, 1U, &a) == 0);
    assert(rawclip_history_add(&h, "beta", 4U, 2U, &b) == 0);
    assert(h.len == 2U);
    assert(h.entries[0].id == b);
    assert(h.entries[1].id == a);

    assert(rawclip_history_add(&h, "alpha", 5U, 3U, &a2) == 0);
    assert(a2 == a);
    assert(h.entries[0].id == a);
    assert(h.entries[0].timestamp_ms == 3U);

    assert(rawclip_history_add(&h, "gamma", 5U, 4U, NULL) == 0);
    assert(h.len == 2U);
    assert(rawclip_history_get(&h, b) == NULL);
    e = rawclip_history_get(&h, a);
    assert(e != NULL && strcmp(e->text, "alpha") == 0);

    errno = 0;
    assert(rawclip_history_add(&h, embedded, sizeof(embedded), 5U, NULL) == -1);
    assert(errno == EINVAL);
    assert(rawclip_history_add(&h, "oversized", 9U, 5U, NULL) == -1);

    rawclip_history_reconfigure(&h, 1U, 4U, true);
    assert(h.len == 0U);

    assert(rawclip_history_get(&h, a) == NULL);
    rawclip_history_clear(&h);
    assert(h.len == 0U);
    rawclip_history_destroy(&h);
    return 0;
}
