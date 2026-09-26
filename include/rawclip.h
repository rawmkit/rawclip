#ifndef RAWCLIP_H
#define RAWCLIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAWCLIP_DEFAULT_MAX_ENTRIES 256U
#define RAWCLIP_DEFAULT_MAX_ENTRY_BYTES (128U * 1024U)
#define RAWCLIP_IPC_MAX_FRAME (2U * 1024U * 1024U)

struct rawclip_entry {
    uint64_t id;
    uint64_t timestamp_ms;
    char *text;
    size_t len;
};

struct rawclip_history {
    struct rawclip_entry *entries;
    size_t len;
    size_t cap;
    size_t max_entries;
    size_t max_entry_bytes;
    uint64_t next_id;
    bool deduplicate;
};

void rawclip_history_init(struct rawclip_history *h, size_t max_entries,
                          size_t max_entry_bytes, bool deduplicate);
void rawclip_history_destroy(struct rawclip_history *h);
int rawclip_history_add(struct rawclip_history *h, const char *text, size_t len,
                        uint64_t timestamp_ms, uint64_t *id_out);
const struct rawclip_entry *rawclip_history_get(const struct rawclip_history *h,
                                                uint64_t id);
int rawclip_history_delete(struct rawclip_history *h, uint64_t id);
void rawclip_history_clear(struct rawclip_history *h);
void rawclip_history_reconfigure(struct rawclip_history *h, size_t max_entries,
                                 size_t max_entry_bytes, bool deduplicate);

#endif
