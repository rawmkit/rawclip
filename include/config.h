#ifndef RAWCLIP_CONFIG_H
#define RAWCLIP_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

struct rawclip_exclude_rule {
    char *instance;
    char *class_name;
    char *title;
    char *executable;
};

struct rawclip_config {
    size_t max_entries;
    size_t max_entry_bytes;
    bool deduplicate;
    bool monitor_clipboard;
    bool monitor_primary;
    bool honor_password_manager_hint;
    char *menu_command;
    char *menu_prompt;
    struct rawclip_exclude_rule *rules;
    size_t rules_len;
};

void rawclip_config_defaults(struct rawclip_config *cfg);
void rawclip_config_destroy(struct rawclip_config *cfg);
int rawclip_config_load(struct rawclip_config *cfg, const char *explicit_path,
                        char *err, size_t err_size);
int rawclip_config_validate_file(const char *path, char *err, size_t err_size);

#endif
