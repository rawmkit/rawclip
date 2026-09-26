rawclipd(1)

# NAME

rawclipd - X11 clipboard history and replay service

# SYNOPSIS

*rawclipd* [*-c* _path_]

*rawclipd* *-C* *-c* _path_

*rawclipd* *-v*

# DESCRIPTION

*rawclipd* monitors configured X11 selections with XFixes, applies capture
policy before requesting text, stores bounded history in memory, including
optional in-memory pin state, and serves selected entries as the CLIPBOARD
owner. Optional policy can synchronize admitted
PRIMARY and CLIPBOARD text in either direction. Direct and ICCCM INCR transfers
are supported in both capture and replay directions. As a selection owner,
rawclipd supports TARGETS, MULTIPLE, and TIMESTAMP alongside supported text
targets.

The daemon remains in the foreground. Session startup and global keyboard
bindings belong to the surrounding desktop/window-manager configuration. The
local control protocol is private to a matching installed *rawclip*(1) client;
restart this long-lived daemon after an upgrade which changes that protocol.

# OPTIONS

*-c* _path_
	Use only the explicit configuration file instead of system/XDG layers.

*-C*
	Validate the file supplied by *-c* without opening the X display or starting
	the daemon.

*-v*
	Print the program version.

# SIGNALS

*SIGHUP*
	Reload configuration atomically. A failed reload leaves existing live
	policy in place. History-limit changes which would evict pinned entries are
	rejected. Any in-flight external capture is aborted before a successfully
	parsed new policy becomes live.

*SIGINT*, *SIGTERM*
	Stop the daemon and destroy in-memory history.

# SECURITY

Offers advertising *x-kde-passwordManagerHint* are queried before text when
the default sensitive policy is enabled. The exact value *secret* is rejected
without requesting clipboard text; an unreadable advertised hint fails closed.
Application exclusion rules are secondary advisory policy based on X11 owner
metadata. Selection synchronization runs only after those admission checks, so
rejected offers are not mirrored to the opposite selection.

Private control and singleton state live under an absolute user-owned
non-group/world-writable XDG_RUNTIME_DIR. Runtime basenames are implementation
detail. The control socket is mode 0600 and validates Linux peer credentials. The
per-X-server singleton lock is normalized to mode 0600. Local client I/O is
bounded so partial or disconnected control exchanges cannot terminate or wedge
the daemon indefinitely.

# EXIT STATUS

0 on normal shutdown or successful validation, 1 on runtime/configuration
failure, and 2 on command-line misuse.


# ENVIRONMENT

*DISPLAY*
	X server monitored by the daemon. X11 selections are server-wide; screen aliases
	of the same server share one rawclip control endpoint.

*XDG_RUNTIME_DIR*
	Required private runtime directory for control and singleton state. It must be
	absolute, owned by the current user, and not group/world writable.

*XDG_CONFIG_HOME*, *HOME*
	Select user configuration when no explicit *-c* path is supplied.

# INTERFACE STABILITY

The documented options, exit-status classes, signal behavior, and configuration
semantics are public 1.x interfaces. Runtime endpoint names, IPC framing/version,
and human diagnostic wording are private. Keep *rawclip*(1) and *rawclipd* from
the same installation and restart the long-lived daemon after an IPC-changing
upgrade.

# SEE ALSO

*rawclip*(1), *rawclip.conf*(5)
