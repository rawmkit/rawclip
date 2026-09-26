rawclip(1)

# NAME

rawclip - control rawclipd and select clipboard history with rawmenu

# SYNOPSIS

*rawclip* [*-c* _path_] _command_ [_argument_]

*rawclip* *-v*

# DESCRIPTION

*rawclip* is the user-facing control client for *rawclipd*(1). It does not
monitor X11 selections itself and does not own persistent history.

# COMMANDS

*menu* [*select*|*delete*|*toggle-pin*]
	Present current history through the configured rawmenu selector. The default
	and explicit *select* action replay the selected entry as CLIPBOARD; *delete*
	removes the selected history entry without changing clipboard ownership;
	*toggle-pin* atomically flips automatic-eviction protection for the selected
	entry. The configured executable is invoked with
	*-M select -p* _prompt_. Empty history is a successful no-op and is reported
	on standard error.

*list*
	Print one line per entry as _ID<TAB>MARKER SPACE PREVIEW_. MARKER is *\**
	for pinned and *-* for unpinned. The ID and marker fields are stable 1.x
	output; the preview is human-oriented.

*show* _id_
	Write the exact stored text bytes for one entry. IDs are nonzero ASCII
	decimal numbers; signed, whitespace-prefixed, zero, or overflowing values are
	command-line misuse.

*select* _id_
	Make rawclipd the CLIPBOARD owner for one history entry. When configured,
	clipboard-to-primary synchronization mirrors the replay to PRIMARY.

*delete* _id_
	Delete one history entry, including a pinned entry.

*pin* _id_
	Protect one history entry from automatic eviction.

*unpin* _id_
	Remove automatic-eviction protection from one history entry.

*clear* [*unpinned*]
	Without an argument, delete all history and release CLIPBOARD or PRIMARY
	selections currently owned by rawclipd. With *unpinned*, remove only unpinned
	history and leave current selection ownership unchanged.

*pause*, *resume*, *toggle*
	Control capture without destroying existing history.

*status*
	Print human-oriented daemon/history policy and live X11 state. Field names,
	order, and formatting are diagnostic rather than a machine-readable ABI.

*health*
	Probe the authenticated daemon control path. Print exactly *ok* followed by a
	newline and exit zero when the daemon services the request from its main event
	loop.

*reload*
	Ask rawclipd to atomically reload its configuration.

# OPTIONS

*-c* _path_
	Use _path_ for local *menu* policy instead of normal system/XDG layers. This
	option is accepted only with the *menu* command and does not change the
	already-running daemon's policy; use *reload* after changing daemon
	configuration.

*-v*
	Print the program version. This is a standalone form.

# EXIT STATUS

0 on success or menu cancellation, 1 on runtime or menu-frontend failure, and
2 on command-line misuse.

The control protocol is private to matching *rawclip*/*rawclipd* versions.
Restart the running daemon after an upgrade which changes that protocol. A
connection reset during the first exchange is diagnosed as a possibly stale or
incompatible daemon rather than as an unexplained socket reset.


# ENVIRONMENT

*DISPLAY*
	Select the X server whose rawclip control endpoint is addressed. Screen aliases
	of the same server share one endpoint.

*XDG_RUNTIME_DIR*
	Required private runtime directory for client/daemon control state. It must be
	absolute, owned by the current user, and not group/world writable.

*XDG_CONFIG_HOME*, *HOME*
	Select user configuration when *-c* is not supplied for *menu*.

*PATH*
	Used by *menu* when the configured menu command is not an absolute path.

# INTERFACE STABILITY

The documented command forms, exit-status classes, exact *show* payload and
*health* success output, and the ID/pin-marker fields of *list* are public 1.x
interfaces. *status*, human diagnostics, chooser presentation, runtime endpoint
names, and the client/daemon IPC protocol are not machine-readable ABIs.

# FILES

_$XDG_RUNTIME_DIR_
	Contains rawclip private per-X-server control and singleton state. Basenames
	are implementation-private. The directory must be absolute, owned by the
	current user, and not group/world writable. On Linux the client verifies the
	connected daemon UID with SO_PEERCRED.

# SEE ALSO

*rawclipd*(1), *rawclip.conf*(5), *rawmenu*(1)
