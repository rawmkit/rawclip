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
    assert(cfg.max_entry_bytes == 1048576U);
    assert(cfg.deduplicate);
    assert(cfg.monitor_clipboard);
    assert(!cfg.monitor_primary);
    assert(cfg.honor_password_manager_hint);
    assert(strcmp(cfg.menu_command, "rawmenu") == 0);
    rawclip_config_destroy(&cfg);
    return 0;
}
