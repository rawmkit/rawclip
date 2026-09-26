#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char **argv)
{
    struct rawclip_config cfg;
    char err[256];
    if (argc != 2) return 2;
    memset(&cfg, 0, sizeof(cfg));
    assert(rawclip_config_load(&cfg, argv[1], err, sizeof(err)) == 0);
    assert(cfg.max_entries == 256U);
    assert(cfg.max_entry_bytes == 131072U);
    assert(cfg.deduplicate);
    assert(cfg.monitor_clipboard);
    assert(!cfg.monitor_primary);
    assert(cfg.sync_mode == RAWCLIP_SYNC_NONE);
    assert(strcmp(rawclip_sync_mode_name(cfg.sync_mode), "none") == 0);
    assert(cfg.honor_password_manager_hint);
    assert(strcmp(cfg.menu_command, "rawmenu") == 0);
    assert(cfg.menu_preview_chars == 200U);
    rawclip_config_destroy(&cfg);
    return 0;
}
