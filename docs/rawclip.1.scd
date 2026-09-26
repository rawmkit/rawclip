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

*menu*
	Present current history through the configured rawmenu selector and replay
	the selected entry as CLIPBOARD.

*list*
	Print stable numeric IDs and single-line human previews.

*show* _id_
	Write the exact stored text bytes for one entry.

*select* _id_
	Make rawclipd the CLIPBOARD owner for one history entry.

*delete* _id_
	Delete one history entry.

*clear*
	Delete all history and release CLIPBOARD when rawclipd currently owns it.

*pause*, *resume*, *toggle*
	Control capture without destroying existing history.

*status*
	Print daemon pause/history status.

*reload*
	Ask rawclipd to atomically reload its configuration.

# OPTIONS

*-c* _path_
	Use _path_ for local menu policy instead of normal system/XDG layers. This
	does not change the already-running daemon's policy; use *reload* after
	changing daemon configuration.

*-v*
	Print the program version.

# EXIT STATUS

0 on success or menu cancellation, 1 on runtime failure, and 2 on command-line
misuse.

# FILES

_$XDG_RUNTIME_DIR/rawclip-DISPLAY.sock_
	Private per-display control socket.

# SEE ALSO

*rawclipd*(1), *rawclip.conf*(5), *rawmenu*(1)
