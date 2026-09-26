# rawclip

`rawclip` is a small X11 clipboard history service for rawmkit.

The project separates clipboard ownership from user interface and window-manager
policy:

* `rawclipd` monitors X11 selections, applies capture policy, owns the in-memory
  history, and serves replayed clipboard data;
* `rawclip` is the local control client and the `rawmenu` frontend;
* `rawmenu` owns interactive selection UI;
* `rawm` should own any global key binding used to invoke `rawclip menu`.

There are no daemon-owned global hotkeys and no clipboard persistence.

## Security model

Clipboard history is an authority boundary, not a passive cache. `rawclipd`
therefore requests `TARGETS` before clipboard text and can refuse a source
without reading its text payload.

By default, an offer advertising the de-facto Linux password-manager target
`x-kde-passwordManagerHint` is queried before text transfer. An exact hint value
of `secret` is excluded from history without requesting the clipboard text. A
readable non-secret hint value does not suppress capture, while an advertised
hint that cannot be retrieved or parsed safely fails closed before text.

Application rules are secondary policy for software which does not advertise a
sensitive target. Rules can match `WM_CLASS` instance/class, window title, and
the executable derived from `_NET_WM_PID`. Every field present in a rule must
match. Owner metadata on X11 is advisory and can be absent or misleading, so
these rules are not a substitute for the sensitive clipboard hint.

History is memory-only. Clearing or exiting `rawclipd` destroys the history.
Deleted history buffers are overwritten before being released, but rawclip does
not claim secure-memory semantics: ordinary clipboard text may have existed in
Xlib, allocator, application, swap, core-dump, or kernel buffers.

See `SECURITY.md` for the full boundary.

## Dependencies

Build dependencies:

* C99 compiler
* Meson >= 1.6.0
* Xlib
* XFixes
* libconfig
* scdoc

Runtime integration:

* X11 with XFixes
* `rawmenu` for `rawclip menu`

`Xvfb` is optional and enables the X11 integration test.

## Build

```sh
meson setup build
meson compile -C build
meson test -C build
```

Install with:

```sh
meson install -C build
```

## Start the daemon

`rawclipd` stays in the foreground. Start it from the session startup authority
used by the desktop:

```sh
rawclipd &
```

The daemon creates one mode-0600 control socket per X display under
`$XDG_RUNTIME_DIR`. The runtime directory must be absolute, owned by the
current user, and not group/world writable. Control clients are authenticated
with Linux peer credentials.

## Commands

```text
rawclip menu
rawclip list
rawclip show ID
rawclip select ID
rawclip delete ID
rawclip clear
rawclip pause
rawclip resume
rawclip toggle
rawclip status
rawclip reload
```

`rawclip list` prints stable history IDs and a single-line human preview.
`rawclip show ID` writes the exact stored text bytes. `rawclip select ID` asks
`rawclipd` to become the X11 `CLIPBOARD` owner for that entry.

`rawclip menu` obtains the current history over IPC, feeds stable IDs plus
sanitized previews to:

```sh
rawmenu -M select -p clipboard
```

and replays the selected ID. No shell evaluates the menu command.

Cancellation is not an error and leaves the current clipboard untouched. An empty
history is also a successful no-op; `rawclip menu` reports `history is empty` on
stderr instead of silently looking like a failed selector launch.

## Window-manager integration

Keep the global key binding in the window manager rather than in the clipboard
service. A rawm binding should invoke:

```sh
rawclip menu
```

The exact rawm configuration belongs to the later rawm-config integration
tranche.

## Configuration

The defaults are compiled in. Without `-c`, `rawclipd` layers:

1. `/etc/rawclip/rawclip.conf`
2. `$XDG_CONFIG_HOME/rawclip/rawclip.conf`, or
   `$HOME/.config/rawclip/rawclip.conf`

An explicit `-c PATH` replaces those external layers.

Validate one file without connecting to X:

```sh
rawclipd -C -c ./rawclip.conf
```

The parser is strict. Unknown top-level or nested settings, wrong scalar types,
empty exclusion rules, and impossible selection policy are errors.

Default policy:

```libconfig
history = {
    max_entries = 256;
    max_entry_bytes = 131072;
    deduplicate = true;
};

selection = {
    clipboard = true;
    primary = false;
};

security = {
    honor_password_manager_hint = true;
};

menu = {
    command = "rawmenu";
    prompt = "clipboard";
};

exclude = ();
```

`PRIMARY` is disabled by default because ordinary X selection semantics can
capture text merely by highlighting it. `CLIPBOARD` corresponds to explicit
copy/paste intent.

### Exclusion rules

Example:

```libconfig
exclude = (
    {
        class = "KeePassXC";
    },
    {
        class = "SomeApplication";
        title = "*Password*";
    },
    {
        executable = "/usr/bin/private-tool";
    }
);
```

Fields use `fnmatch(3)` shell-style patterns. All fields present in one group
must match for the source to be excluded.

## Text contract

0.1.0 is intentionally text-only.

rawclip accepts modern `UTF8_STRING` and legacy `STRING` offers. UTF-8 is
validated before history admission. `STRING` data is interpreted as ISO-8859-1
and normalized to UTF-8. Replay advertises `UTF8_STRING`; `STRING` is produced
only when every stored code point is representable as ISO-8859-1.

Incoming X11 INCR transfers are supported. History admission is bounded by both
the configured entry limit and the current X server's direct property request
limit, because 0.1.0 deliberately does not implement outgoing INCR replay.
Oversized selections are ignored rather than truncated.

Images, HTML, URI lists and arbitrary MIME graphs are out of scope for 0.1.0.

## Persistence

There is none.

This is deliberate. A clipboard manager that silently creates a plaintext
long-lived database changes the security model of every application on the
desktop. Persistent history, if ever added, must be an explicit separately
audited authority rather than a hidden default.

## Exit status

`rawclip` and `rawclipd` use:

* `0` for success or menu cancellation;
* `1` for runtime/configuration/daemon failures;
* `2` for command-line misuse.

Human-readable diagnostics are not a machine-readable protocol. Local IPC is
private to matching rawclip/rawclipd versions and is not a public ABI.
