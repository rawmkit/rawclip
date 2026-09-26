rawclip.conf(5)

# NAME

rawclip.conf - rawclip clipboard history policy

# DESCRIPTION

Configuration uses libconfig syntax. Defaults are compiled in. Without an
explicit *-c* path, rawclip layers /etc/rawclip/rawclip.conf and the XDG user
configuration.

Unknown keys and wrong scalar types are errors.

# HISTORY

*history.max_entries* = integer
	Maximum in-memory history length. Default: 256.

*history.max_entry_bytes* = integer
	Maximum text payload size before X-server replay limits are applied.
	Default: 131072.

*history.deduplicate* = boolean
	Move duplicate text to the newest position while retaining its stable ID.
	Default: true.

# SELECTION

*selection.clipboard* = boolean
	Monitor CLIPBOARD. Default: true.

*selection.primary* = boolean
	Monitor PRIMARY. Default: false.

At least one monitored selection must remain enabled.

# SECURITY

*security.honor_password_manager_hint* = boolean
	Query x-kde-passwordManagerHint before text transfer. Drop an exact value
	*secret* and fail closed if an advertised hint cannot be read safely.
	Default: true.

# MENU

*menu.command* = string
	Executable invoked directly for *rawclip menu*. It must implement the rawmenu
	selector command-line contract and is invoked with *-M select -p* _prompt_.
	Default: "rawmenu".

*menu.prompt* = string
	Prompt passed to the menu frontend. Default: "clipboard".

# EXCLUDE

*exclude* is a list of groups. A group may contain *instance*, *class*, *title*,
and *executable* string patterns. Every field present in a group must match.
Patterns use fnmatch(3) semantics.

Example:

```
exclude = (
    { class = "KeePassXC"; },
    { class = "Browser"; title = "*Private*"; }
);
```

Owner metadata is advisory and is not an authenticated security identity.
Prefer the password-manager hint when the source application supports it.

# FILES

_/etc/rawclip/rawclip.conf_
	System policy.

_$XDG_CONFIG_HOME/rawclip/rawclip.conf_
	User policy when XDG_CONFIG_HOME is absolute.

_$HOME/.config/rawclip/rawclip.conf_
	User fallback.

# SEE ALSO

*rawclip*(1), *rawclipd*(1), *fnmatch*(3)
