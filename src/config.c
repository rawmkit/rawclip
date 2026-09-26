#define _POSIX_C_SOURCE 200809L

#include "config.h"
#include "rawclip.h"
#include "util.h"

#include <errno.h>
#include <libconfig.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
set_error(char *err, size_t size, const char *fmt, ...)
{
    va_list ap;
    if (err == NULL || size == 0U) {
        return;
    }
    va_start(ap, fmt);
    (void)vsnprintf(err, size, fmt, ap);
    va_end(ap);
}

static char *
dup_string(const char *s)
{
    return s == NULL ? NULL : rawclip_xstrndup(s, strlen(s));
}

void
rawclip_config_defaults(struct rawclip_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_entries = RAWCLIP_DEFAULT_MAX_ENTRIES;
    cfg->max_entry_bytes = RAWCLIP_DEFAULT_MAX_ENTRY_BYTES;
    cfg->deduplicate = true;
    cfg->monitor_clipboard = true;
    cfg->monitor_primary = false;
    cfg->honor_password_manager_hint = true;
    cfg->menu_command = dup_string("rawmenu");
    cfg->menu_prompt = dup_string("clipboard");
}

void
rawclip_config_destroy(struct rawclip_config *cfg)
{
    size_t i;
    if (cfg == NULL) {
        return;
    }
    free(cfg->menu_command);
    free(cfg->menu_prompt);
    for (i = 0U; i < cfg->rules_len; i++) {
        free(cfg->rules[i].instance);
        free(cfg->rules[i].class_name);
        free(cfg->rules[i].title);
        free(cfg->rules[i].executable);
    }
    free(cfg->rules);
    memset(cfg, 0, sizeof(*cfg));
}

static int
replace_string(char **dst, const char *src)
{
    char *p = dup_string(src);
    if (p == NULL) {
        return -1;
    }
    free(*dst);
    *dst = p;
    return 0;
}

static int
read_size(config_t *c, const char *path, size_t *out, size_t min, size_t max,
          char *err, size_t err_size)
{
    long long value;
    if (config_lookup_int64(c, path, &value) == CONFIG_FALSE) {
        return 0;
    }
    if (value < 0 || (unsigned long long)value < min ||
        (unsigned long long)value > max) {
        set_error(err, err_size, "%s is outside the supported range", path);
        errno = EINVAL;
        return -1;
    }
    *out = (size_t)value;
    return 0;
}

static int
parse_rules(config_t *c, struct rawclip_config *cfg, char *err, size_t err_size)
{
    config_setting_t *list = config_lookup(c, "exclude");
    int i, n;

    if (list == NULL) {
        return 0;
    }
    if (config_setting_type(list) != CONFIG_TYPE_LIST) {
        set_error(err, err_size, "exclude must be a list");
        errno = EINVAL;
        return -1;
    }
    n = config_setting_length(list);
    if (n < 0 || n > 128) {
        set_error(err, err_size, "exclude contains too many rules");
        errno = EINVAL;
        return -1;
    }
    {
        size_t old_i;
        for (old_i = 0U; old_i < cfg->rules_len; old_i++) {
            free(cfg->rules[old_i].instance);
            free(cfg->rules[old_i].class_name);
            free(cfg->rules[old_i].title);
            free(cfg->rules[old_i].executable);
        }
        free(cfg->rules);
        cfg->rules = NULL;
        cfg->rules_len = 0U;
    }
    if (n == 0) {
        return 0;
    }
    cfg->rules = calloc((size_t)n, sizeof(*cfg->rules));
    if (cfg->rules == NULL) {
        return -1;
    }
    cfg->rules_len = (size_t)n;
    for (i = 0; i < n; i++) {
        config_setting_t *rule = config_setting_get_elem(list, i);
        const char *value;
        bool any = false;
        if (rule == NULL || config_setting_type(rule) != CONFIG_TYPE_GROUP) {
            set_error(err, err_size, "exclude[%d] must be a group", i);
            errno = EINVAL;
            return -1;
        }
#define RULE_STRING(KEY, FIELD) \
        do { \
            if (config_setting_lookup_string(rule, KEY, &value) == CONFIG_TRUE) { \
                cfg->rules[(size_t)i].FIELD = dup_string(value); \
                if (cfg->rules[(size_t)i].FIELD == NULL) return -1; \
                any = true; \
            } \
        } while (0)
        RULE_STRING("instance", instance);
        RULE_STRING("class", class_name);
        RULE_STRING("title", title);
        RULE_STRING("executable", executable);
#undef RULE_STRING
        if (!any) {
            set_error(err, err_size, "exclude[%d] has no match fields", i);
            errno = EINVAL;
            return -1;
        }
        {
            int j;
            for (j = 0; j < config_setting_length(rule); j++) {
                config_setting_t *field = config_setting_get_elem(rule, j);
                const char *name = field == NULL ? NULL : config_setting_name(field);
                if (name == NULL || (strcmp(name, "instance") != 0 && strcmp(name, "class") != 0 &&
                                     strcmp(name, "title") != 0 && strcmp(name, "executable") != 0)) {
                    set_error(err, err_size, "exclude[%d] contains unknown fields", i);
                    errno = EINVAL;
                    return -1;
                }
            }
        }
    }
    return 0;
}

static int
apply_config(config_t *c, struct rawclip_config *cfg, char *err, size_t err_size)
{
    int b;
    const char *s;
    config_setting_t *root = config_root_setting(c);
    config_setting_t *history;
    config_setting_t *selection;
    config_setting_t *security;
    config_setting_t *menu;
    int allowed = 0;

    if ((history = config_lookup(c, "history")) != NULL) allowed++;
    if ((selection = config_lookup(c, "selection")) != NULL) allowed++;
    if ((security = config_lookup(c, "security")) != NULL) allowed++;
    if ((menu = config_lookup(c, "menu")) != NULL) allowed++;
    if (config_lookup(c, "exclude") != NULL) allowed++;
    if (config_setting_length(root) != allowed) {
        set_error(err, err_size, "configuration contains unknown top-level settings");
        errno = EINVAL;
        return -1;
    }
    if (history != NULL && config_setting_type(history) != CONFIG_TYPE_GROUP) goto bad_group;
    if (selection != NULL && config_setting_type(selection) != CONFIG_TYPE_GROUP) goto bad_group;
    if (security != NULL && config_setting_type(security) != CONFIG_TYPE_GROUP) goto bad_group;
    if (menu != NULL && config_setting_type(menu) != CONFIG_TYPE_GROUP) goto bad_group;

    if (read_size(c, "history.max_entries", &cfg->max_entries, 1U, 65536U, err, err_size) != 0) return -1;
    if (read_size(c, "history.max_entry_bytes", &cfg->max_entry_bytes, 1U, RAWCLIP_IPC_MAX_FRAME / 2U, err, err_size) != 0) return -1;
    if (config_lookup_bool(c, "history.deduplicate", &b)) cfg->deduplicate = b != 0;
    if (config_lookup_bool(c, "selection.clipboard", &b)) cfg->monitor_clipboard = b != 0;
    if (config_lookup_bool(c, "selection.primary", &b)) cfg->monitor_primary = b != 0;
    if (!cfg->monitor_clipboard && !cfg->monitor_primary) {
        set_error(err, err_size, "at least one selection must be monitored");
        errno = EINVAL;
        return -1;
    }
    if (config_lookup_bool(c, "security.honor_password_manager_hint", &b)) cfg->honor_password_manager_hint = b != 0;
    if (config_lookup_string(c, "menu.command", &s) && replace_string(&cfg->menu_command, s) != 0) return -1;
    if (config_lookup_string(c, "menu.prompt", &s) && replace_string(&cfg->menu_prompt, s) != 0) return -1;
    if (cfg->menu_command == NULL || *cfg->menu_command == '\0') {
        set_error(err, err_size, "menu.command must not be empty");
        errno = EINVAL;
        return -1;
    }
    return parse_rules(c, cfg, err, err_size);

bad_group:
    set_error(err, err_size, "configuration sections must be groups");
    errno = EINVAL;
    return -1;
}

static int
read_one(struct rawclip_config *cfg, const char *path, bool required,
         char *err, size_t err_size)
{
    config_t c;
    int rc = 0;
    config_init(&c);
    if (config_read_file(&c, path) == CONFIG_FALSE) {
        if (!required && config_error_type(&c) == CONFIG_ERR_FILE_IO && errno == ENOENT) {
            config_destroy(&c);
            return 0;
        }
        set_error(err, err_size, "%s:%d: %s", path, config_error_line(&c), config_error_text(&c));
        config_destroy(&c);
        errno = EINVAL;
        return -1;
    }
    rc = apply_config(&c, cfg, err, err_size);
    config_destroy(&c);
    return rc;
}

int
rawclip_config_load(struct rawclip_config *cfg, const char *explicit_path,
                    char *err, size_t err_size)
{
    const char *xdg;
    const char *home;
    char path[PATH_MAX];

    rawclip_config_defaults(cfg);
    if (cfg->menu_command == NULL || cfg->menu_prompt == NULL) {
        return -1;
    }
    if (explicit_path != NULL) {
        return read_one(cfg, explicit_path, true, err, err_size);
    }
    if (read_one(cfg, "/etc/rawclip/rawclip.conf", false, err, err_size) != 0) {
        return -1;
    }
    xdg = getenv("XDG_CONFIG_HOME");
    home = getenv("HOME");
    if (xdg != NULL && xdg[0] == '/') {
        if (snprintf(path, sizeof(path), "%s/rawclip/rawclip.conf", xdg) >= (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return read_one(cfg, path, false, err, err_size);
    }
    if (home != NULL && home[0] == '/') {
        if (snprintf(path, sizeof(path), "%s/.config/rawclip/rawclip.conf", home) >= (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        return read_one(cfg, path, false, err, err_size);
    }
    return 0;
}

int
rawclip_config_validate_file(const char *path, char *err, size_t err_size)
{
    struct rawclip_config cfg;
    int rc;
    memset(&cfg, 0, sizeof(cfg));
    rc = rawclip_config_load(&cfg, path, err, err_size);
    rawclip_config_destroy(&cfg);
    return rc;
}
