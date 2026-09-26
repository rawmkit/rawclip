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

Incoming INCR transfers are accumulated under the configured byte bound.
Malformed type changes, invalid UTF-8, oversized payloads, empty payloads, and
owner races are discarded rather than partially stored.

Source windows may disappear during metadata queries. rawclip installs a
non-fatal X error handler so asynchronous `BadWindow` races do not invoke
Xlib's default process-terminating handler. A fatal X I/O failure still ends
the process; reconnect/recovery is not attempted.

## Replay authority

Selecting a history entry makes rawclipd the X11 `CLIPBOARD` owner. It serves
`TARGETS`, `TIMESTAMP`, `UTF8_STRING`, and representable `STRING` requests.

rawclip does not yet implement outgoing INCR. Captured items are therefore
bounded by
a conservative limit derived from the X server's maximum direct request size.
Items which cannot later be served safely are not admitted.

## Local control socket

The socket lives under `$XDG_RUNTIME_DIR`, which must be absolute, owned by the
current user, and not group/world writable. The socket mode is 0600.

On Linux, rawclipd verifies `SO_PEERCRED` and requires the connecting process UID
to match the daemon UID. IPC frames contain a fixed magic, version, type and
32-bit bounded length in network byte order. Oversized or malformed frames are
rejected before payload allocation.

The IPC protocol is private implementation detail and may change between
pre-1.0 releases.

## Configuration

libconfig policy is strict. Unknown keys and wrong scalar types are errors.
An explicit `-c` file replaces system/XDG layers. Reload parses a complete new
configuration before replacing live policy.

Tightening history limits during reload immediately evicts entries which no
longer satisfy the new bounds.

## Menu boundary

rawclip executes the configured menu program directly with an argv vector. It
does not invoke a shell. The initial integration expects a rawmenu-compatible:

```text
COMMAND select -p PROMPT
```

The menu receives sanitized previews rather than raw multiline text, prefixed
with stable numeric IDs. The selected ID is revalidated by rawclipd before
clipboard ownership changes.

rawclip does not register global shortcuts. Keyboard authority belongs to the
window manager.

## Out of scope for 0.1.0

* persistence or synchronized history;
* encryption-at-rest;
* arbitrary MIME/image/HTML history;
* outgoing INCR replay;
* Wayland;
* global hotkeys;
* automatic clipboard expiry;
* cryptographic authentication of X11 owner metadata;
* protection from other clients already authorized to the X server.
