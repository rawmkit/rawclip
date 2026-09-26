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
`$XDG_RUNTIME_DIR`. A stable per-display mode-0600 lock file serializes daemon
startup and is intentionally retained in the runtime directory across daemon
exits. Both `rawclipd` and `rawclip` require the runtime directory to be absolute,
owned by the current user, and not group/world writable. On Linux each side
verifies the peer UID, so neither the daemon nor the control client accepts a
local endpoint owned by another user.

## Commands

```text
rawclip menu
rawclip menu delete
rawclip menu toggle-pin
rawclip list
rawclip show ID
rawclip select ID
rawclip delete ID
rawclip pin ID
rawclip unpin ID
rawclip clear
rawclip clear unpinned
rawclip pause
rawclip resume
rawclip toggle
rawclip status
rawclip health
rawclip reload
```

`rawclip list` prints stable history IDs, a pin marker (`*` pinned, `-`
unpinned), and a single-line human preview. Entry IDs accepted on the command
line are nonzero ASCII decimal numbers; signs, whitespace and overflow are
command-line misuse. `rawclip show ID` writes the exact stored text bytes.
`rawclip select ID` asks `rawclipd` to become the X11 `CLIPBOARD` owner for that
entry.

`rawclip pin ID` protects an entry from automatic history eviction. `unpin`
removes that protection. Pin state is intentionally memory-only and disappears
with the daemon. Deduplication preserves an existing pin while moving the
duplicate to the newest position. If history is at capacity and every retained
entry is pinned, a new distinct capture is dropped rather than evicting pinned
state.

`rawclip clear` retains its original destructive meaning: delete every history
entry and release selections currently owned by rawclipd. `rawclip clear
unpinned` removes only unpinned history and does not change current selection
ownership. Explicit `delete ID` may delete a pinned entry.

`rawclip menu` obtains the current history over IPC, feeds stable IDs plus
sanitized previews to:

```sh
rawmenu -M select -p clipboard
```

and replays the selected ID. `rawclip menu delete` uses the same selector to
remove one chosen history entry without changing clipboard ownership. `rawclip
menu toggle-pin` atomically flips the selected entry's pin state. Pin markers are
shown in the chooser, but are not part of stored clipboard text. No shell
evaluates the menu command.

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

The parser is strict. Unknown top-level or nested settings, wrong scalar types
(including every supplied exclusion-rule field), empty exclusion rules, and
impossible selection policy are errors.

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
    synchronize = "none";
};

security = {
    honor_password_manager_hint = true;
};

menu = {
    command = "rawmenu";
    prompt = "clipboard";
    preview_chars = 200;
};

exclude = ();
```

`PRIMARY` is disabled by default because ordinary X selection semantics can
capture text merely by highlighting it. `CLIPBOARD` corresponds to explicit
copy/paste intent. Synchronization is also disabled by default.

`selection.synchronize` accepts `none`, `primary-to-clipboard`,
`clipboard-to-primary`, and `bidirectional`. A direction is valid only when its
source selection is monitored. Synchronization happens only after an offer has
passed sensitive-hint, exclusion, text-validation, and history-admission policy.
Secret or excluded offers therefore never become authority for the opposite
selection.

rawclip suppresses its own XFixes ownership events and avoids re-owning an
already identical destination value. Explicit history replay remains CLIPBOARD
authority; when clipboard-to-primary synchronization is enabled the same replay
is also mirrored to PRIMARY. Reload first aborts any in-flight external capture,
then installs the new policy, so bytes admitted under an older exclusion,
sensitive, selection or size policy cannot complete under the new policy epoch.
Tightening synchronization policy releases selection ownership acquired only by
synchronization while preserving explicit CLIPBOARD replay.

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

rawclip is intentionally text-only.

rawclip accepts modern `UTF8_STRING` and legacy `STRING` offers. UTF-8 is
validated before history admission. `STRING` data is interpreted as ISO-8859-1
and normalized to UTF-8. Replay advertises `UTF8_STRING`; `STRING` is produced
only when every stored code point is representable as ISO-8859-1.

Both incoming and outgoing X11 `INCR` transfers are supported. Incoming
framing is validated exactly: the INCR header must have the required 32-bit
shape and every data chunk, including the zero-length terminator, must retain the
negotiated text type and 8-bit format. History admission is bounded by
`history.max_entry_bytes`, not by the X server's single-request property limit.
When rawclip owns a selection, values larger than a safe direct property update
are streamed incrementally to requestors. Outgoing transfers are bounded and
abandoned requestors are discarded. Oversized selections are ignored rather
than truncated.

Images, HTML, URI lists and arbitrary MIME graphs remain out of scope.

## Selection policy

`PRIMARY` can be monitored independently without synchronization. Synchronizing
PRIMARY to CLIPBOARD is a stronger policy: highlighted text can become explicitly
pasteable without a copy action. For that reason every synchronization direction
is opt-in and `none` remains the default.

`rawclip status` reports live monitor flags, synchronization mode, history and
pinned-entry counts, current rawclip-owned selections, pending receive state,
and outgoing INCR transfer count. `rawclip health` is the smaller supervision
probe: success means the daemon accepted and serviced an authenticated request
in its event loop. Local control exchanges have bounded socket I/O and a client
which disconnects before a reply cannot terminate the daemon.

Tightening history limits on reload may evict unpinned entries immediately. A
reload that would require evicting a pinned entry, or make a pinned entry exceed
`history.max_entry_bytes`, fails and leaves the previous live policy/history
authority in place.

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

`-v` is a standalone form. `rawclip -c PATH` is valid only with `menu`; it is
local menu policy and never changes daemon configuration.

Human-readable diagnostics are not a machine-readable protocol. Local IPC is
private to matching rawclip/rawclipd versions and is not a public ABI.
