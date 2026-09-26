# Security model

## Scope

rawclip is an X11 clipboard history service for ordinary text. X11 selections
are inherently shared desktop-session state: any client permitted to connect to
the X server can usually request clipboard contents, claim selections, forge
owner metadata, and interfere with transfers. rawclip does not turn X11 into a
confidential transport.

The security objective is narrower:

* do not persist clipboard history;
* do not record offers explicitly marked sensitive;
* let users exclude additional source applications before payload transfer;
* bound every clipboard and IPC allocation;
* keep clipboard history/control authority in one user-scoped daemon;
* avoid global hotkeys, shells, and implicit desktop authority;
* fail closed on malformed policy and protocol input.

## Sensitive clipboard offers

rawclip first requests `TARGETS`. If the source advertises
`x-kde-passwordManagerHint` and `honor_password_manager_hint` is enabled,
rawclip requests that hint target before any text target. An exact 8-bit value
`secret` drops the offer without requesting clipboard text. A readable
non-secret value permits normal capture. If an advertised hint cannot be read
or is malformed, rawclip fails closed and does not request the text payload.

The target is a convention, not an authenticated security label. A malicious
or unaware application can omit it. A clipboard manager cannot infer secrecy
reliably from text contents.

Application exclusion rules therefore provide a secondary mechanism, but X11
window metadata is advisory and can be missing, stale, inherited from helper
windows, or deliberately forged.

## rawpass integration

rawpass 1.1.0 and later advertises `x-kde-passwordManagerHint` with the exact
value `secret` while serving password or OTP material. With the default rawclip
policy, that selection is therefore rejected before its secret text is
requested. No rawpass-specific process-name or window-class exception is
required.

## History memory

History is process memory only. Entry count and byte size are bounded. Duplicate
text can be coalesced without changing its stable entry ID. Deleted buffers are
overwritten before free.

This is hygiene, not a secure-memory guarantee. Copies can still exist in Xlib,
libc, the kernel, source/destination applications, swap, debugging facilities,
or crash dumps.

## X11 transfer admission

The monitor observes ownership changes with XFixes. Before text transfer it can
reject an owner using sensitive-target metadata or configured owner rules.

`UTF8_STRING` input is validated. `STRING` is interpreted as ISO-8859-1 and
normalized to UTF-8. Embedded NUL bytes are rejected.

Incoming INCR transfers are accumulated under the configured byte bound. The
initial INCR property must have the required 32-bit shape, and every subsequent
chunk including the terminal empty property must retain the negotiated text type
and 8-bit format. Malformed framing or type changes, invalid UTF-8, oversized
payloads, empty payloads, and owner races are discarded rather than partially
stored.

Source windows may disappear during metadata queries. rawclip installs a
non-fatal X error handler so asynchronous `BadWindow` races do not invoke
Xlib's default process-terminating handler. A fatal X I/O failure still ends
the process; reconnect/recovery is not attempted.

## Replay authority

Selecting a history entry makes rawclipd the X11 `CLIPBOARD` owner. It serves
`TARGETS`, `TIMESTAMP`, `UTF8_STRING`, and representable `STRING` requests.

Payloads that fit a safe X server property update are served directly. Larger
payloads use ICCCM `INCR`: each accepted requestor receives a private snapshot
and advances the transfer by deleting each property chunk. Outgoing transfers
are bounded to a small fixed concurrency limit and inactive transfers are
expired on later transfer activity. Requestor destruction, selection loss,
explicit clipboard clearing, and replacement by a newly selected history item
all discard associated transfer state.

The configured history byte bound, rather than the X server's direct request
size, is therefore the admission authority for both incoming and replayed text.

## Selection synchronization

PRIMARY and CLIPBOARD are independent X11 selections. Monitoring a selection
only admits its ordinary text into history; synchronization additionally makes
rawclip the owner of the configured destination selection. Synchronization is
therefore disabled by default.

The supported directions are `primary-to-clipboard`, `clipboard-to-primary`,
and `bidirectional`. Each direction requires its source selection to be monitored.
An admitted source is synchronized only after sensitive-hint policy, application
exclusion, text validation, and bounded history admission have succeeded. A
`secret` password-manager offer, malformed hint, excluded owner, or invalid text
never reaches the synchronization path.

Self-generated XFixes owner notifications are ignored and an identical value
already owned by rawclip is not reasserted. This suppresses rawclip's own
PRIMARY/CLIPBOARD feedback loop; it does not attempt to coordinate policy with
other clipboard synchronizers in the same X session.

Ownership records distinguish explicit CLIPBOARD history replay from ownership
created by synchronization. When a reload disables or reverses synchronization,
rawclip releases stale synchronization-owned selections but preserves explicit
CLIPBOARD replay.

## Local control socket

The socket lives under `$XDG_RUNTIME_DIR`, which must be absolute, owned by the
current user, and not group/world writable. The socket mode is 0600. A stable mode-0600 lock file in the same runtime
directory serializes daemon startup; it is reused rather than unlinked on exit
so lock authority cannot move to a new inode during a successor race.

On Linux, peer authentication is symmetric: rawclipd verifies the connecting
client UID with `SO_PEERCRED`, while rawclip verifies that the connected daemon
UID matches its own. Both sides reject an unsafe runtime directory before using
the socket. IPC frames contain a fixed magic, version, type and 32-bit bounded
length in network byte order. Oversized or malformed frames are rejected before
payload allocation. Accepted daemon-side clients have bounded send/receive I/O;
a stalled partial frame or a client which closes before its reply is contained
to that exchange rather than terminating the service.

The IPC protocol is private implementation detail and may change between
pre-1.0 releases.

## Configuration

libconfig policy is strict. Unknown keys and wrong scalar types are errors,
including mistyped fields inside exclusion rules. An explicit `-c` file replaces
system/XDG layers. Reload parses a complete new configuration, aborts any
in-flight external selection receive, then replaces live policy. This prevents a
transfer admitted under an older policy epoch from completing under new
exclusion, sensitive, selection or byte-limit policy.

Tightening history limits during reload may immediately evict unpinned entries
which no longer satisfy the new bounds. Pinned entries are automatic-eviction
barriers: reload fails rather than evicting them or accepting a byte limit below
a pinned entry's size. If every slot is pinned, a new distinct capture is
dropped without synchronization and without terminating the daemon.

Pins are memory-only policy, not persistence. They protect against automatic
eviction only. Explicit `delete` and full `clear` remain authoritative over
pinned entries; `clear unpinned` preserves them.

## Menu boundary

rawclip executes the configured menu program directly with an argv vector. It
does not invoke a shell. The initial integration expects a rawmenu-compatible:

```text
COMMAND -M select -p PROMPT
```

The menu receives sanitized previews rather than raw multiline text, prefixed
with stable numeric IDs and a human pin marker. Preview length is bounded by
`menu.preview_chars` and truncation is UTF-8 character-safe. The selected ID is
revalidated by rawclipd before clipboard ownership, deletion, or pin state
changes. Pin toggling is atomic in the single daemon authority. Menu exit status
1 is cancellation; other frontend failures and malformed successful output are
reported as errors.

rawclip does not register global shortcuts. Keyboard authority belongs to the
window manager.

## Out of scope

* persistent or cross-session synchronized history;
* encryption-at-rest;
* arbitrary MIME/image/HTML history;
* Wayland;
* global hotkeys;
* automatic clipboard expiry;
* cryptographic authentication of X11 owner metadata;
* protection from other clients already authorized to the X server.
