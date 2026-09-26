rawclip 0.7.1 — 2026-09-26
==========================

Upgrade diagnostics
-------------------

* Diagnose a control connection reset during the first exchange as a stale or
  incompatible long-lived rawclipd and direct the user to restart the daemon
  after upgrading rawclip.
* Distinguish an explicit IPC frame-version mismatch from generic malformed
  protocol input without carrying backward-compatibility parsing.
* Document rawclip/rawclipd as a matching installed pair and make daemon restart
  requirements across private IPC revisions explicit.

Testing
-------

* Add a stale-daemon regression which reproduces the pre-v2 behavior of
  accepting the Unix socket, rejecting the newer request header, and closing
  before a reply.

rawclip 0.7.0 — 2026-09-26
==========================

Policy and X11 qualification
----------------------------

* Reject wrong scalar types in every supplied exclusion-rule field instead of
  silently ignoring a mistyped field when another valid field is present.
* Fence configuration policy epochs by aborting an in-flight external selection
  receive before a successfully parsed reload becomes live. Bytes admitted under
  old exclusion, sensitive, selection, or size policy cannot complete under the
  replacement policy.
* Validate incoming ICCCM INCR framing exactly, including the 32-bit header
  shape and the negotiated type/8-bit format of the terminal empty property.
* Drain Xlib's already-buffered event queue before sleeping in poll(2), so
  ownership changes consumed by synchronous Xlib calls cannot wait indefinitely
  for unrelated kernel activity.

Local control authority
-----------------------

* Apply XDG_RUNTIME_DIR ownership/mode checks on both daemon and client and
  authenticate the connected daemon UID as well as incoming client UIDs with
  Linux SO_PEERCRED.
* Normalize the persistent singleton lock to mode 0600.
* Bound accepted control-client send/receive I/O and ignore SIGPIPE so partial,
  stalled, or disconnected exchanges remain local request failures rather than
  terminating the daemon.
* Treat exceptional poll/accept states as operational failures instead of
  allowing event-loop spin or ambiguous service state.

CLI and menu contract
---------------------

* Accept entry IDs only as nonzero ASCII decimal with explicit overflow checks;
  signs, whitespace, zero, and overflow are command-line misuse with status 2.
* Make -v a standalone form and reject rawclip -c PATH for commands other than
  menu, where the path is local frontend policy rather than daemon policy.
* Remove an accidental 255-byte limit from derived delete/pin menu prompts;
  action prompts now preserve any otherwise-valid configured prompt length.

Testing
-------

* Cover policy reload during an admitted text request, malformed INCR headers and
  terminators, unsafe runtime directories, peer credentials, singleton lock mode,
  aborted replies, partial control frames, daemon survival, and long menu prompts
  under the real XFixes/Xvfb integration path.

rawclip 0.6.0 — 2026-09-26
==========================

Pinned history
--------------

* Add in-memory pin state which protects selected entries from automatic
  capacity and byte-limit eviction without making them persistent or immune to
  explicit deletion.
* Evict the oldest unpinned entry when history reaches capacity. If every slot
  is pinned, reject a new distinct capture without terminating the daemon or
  synchronizing the rejected value to the opposite X11 selection.
* Preserve pin state across deduplication while retaining stable IDs and
  newest-first ordering.
* Refuse configuration reloads whose tighter history limits would evict or
  truncate pinned entries; the previous live policy remains authoritative.

Control and UI
--------------

* Add `rawclip pin ID`, `unpin ID`, `menu toggle-pin`, and `clear unpinned`.
  Existing full `clear` semantics remain unchanged.
* Mark pinned entries in `list` and rawmenu previews and expose the pinned count
  through `status`.
* Bump the private local IPC protocol to version 2 so pin flags and controls
  cannot be misread by a mismatched pre-0.6 client or daemon.

Testing
-------

* Cover pinned eviction, deduplication, selective clear, menu toggling,
  protected-full capture, daemon survival, reload conflicts, and the
  synchronization boundary under Xvfb.

rawclip 0.5.0 — 2026-09-26
==========================

History UI
----------

* Add `rawclip menu delete` beside the existing replay action so one history
  entry can be removed through the same rawmenu selector without introducing a
  second UI authority.
* Move menu preview length into strict `menu.preview_chars` policy and truncate
  by UTF-8 characters rather than raw bytes.
* Treat rawmenu status 1 as cancellation while surfacing exec failures, crashes,
  nonzero operational exits, and empty successful output as errors.

Diagnostics and lifecycle
-------------------------

* Add `rawclip health` as a small authenticated event-loop probe and extend
  `status` with receive state, outgoing INCR count, and live rawclip-owned
  CLIPBOARD/PRIMARY selections.
* Keep the per-display singleton lock file as a stable runtime inode and unlink
  the control socket only from the daemon instance that actually bound it.
  A losing second daemon can no longer remove the first daemon's live endpoint.
* Preserve the running policy after failed reloads and exercise rapid X11 owner
  turnover without allowing pending receive state to wedge the daemon.

Testing
-------

* Cover menu selection/deletion, UTF-8 preview truncation, cancellation and
  frontend failures, singleton startup, health probing, failed reload, and
  rapid selection churn under Xvfb.

rawclip 0.4.0 — 2026-09-26
==========================

Selection policy
----------------

* Add explicit `selection.synchronize` policy with `none`,
  `primary-to-clipboard`, `clipboard-to-primary`, and `bidirectional` modes.
  Synchronization remains disabled by default and each direction requires its
  source selection to be monitored.
* Keep PRIMARY monitoring independent from synchronization, so highlighted text
  can enter history without becoming CLIPBOARD authority when policy is `none`.
* Synchronize only text which has already passed sensitive-hint, owner-exclusion,
  text-validation, and bounded history-admission policy. Secret or rejected
  offers never become authority for the opposite selection.

X11 ownership
-------------

* Track CLIPBOARD and PRIMARY ownership independently and serve either selection
  through the same direct/INCR replay path.
* Suppress rawclip's own XFixes ownership events and avoid reasserting identical
  destination values, preventing the daemon from feeding its own synchronized
  selections back into history.
* Record whether ownership came from explicit CLIPBOARD replay or synchronization.
  Tightening policy on reload releases synchronization-owned selections while
  preserving explicit replay authority.
* Associate outgoing incremental transfers with their selection so losing one
  selection cancels only the transfer state governed by that selection.

Diagnostics and testing
-----------------------

* Extend `rawclip status` with the live CLIPBOARD/PRIMARY monitor flags and the
  canonical synchronization mode.
* Exercise PRIMARY-only history, both bidirectional paths, one-way directionality,
  synchronized history replay, secret refusal on both selections, self-event
  suppression, and policy reload under Xvfb.

rawclip 0.3.0 — 2026-09-26
==========================

X11 transfer authority
----------------------

* Serve replayed UTF8_STRING and representable STRING values through ICCCM
  `INCR` when they exceed a safe direct X property update.
* Snapshot large replay payloads per requestor, bound concurrent transfer state,
  expire inactive transfers on later activity, and discard transfer state when
  requestor windows disappear or CLIPBOARD authority changes.
* Remove the X server direct-request limit from capture admission. Incoming
  direct and INCR values are governed by `history.max_entry_bytes`; the server
  request size now only determines outgoing chunk size.

Testing
-------

* Exercise a 400 KiB value through incoming INCR, history storage, outgoing
  UTF8_STRING and STRING INCR replay, and two simultaneous requestors.
* Repeatedly abort requestors mid-transfer and require later replay to succeed,
  covering bounded-state cleanup rather than only successful transfers.
* Probe TARGETS, TIMESTAMP, and unsupported-target behavior while rawclip owns
  CLIPBOARD.

Documentation
-------------

* Define the bidirectional transfer boundary and record automatic PRIMARY to
  CLIPBOARD synchronization as a separate future opt-in policy.

rawclip 0.2.1 — 2026-09-26
==========================

Menu integration
----------------

* Invoke current rawmenu through its public `-M select -p PROMPT` frontend
  contract instead of the invalid positional `select` spelling.
* Make the Xvfb menu stub assert the exact selector argv so rawmenu CLI drift is
  caught by integration coverage.
* Report an empty in-memory history on stderr while preserving it as a successful
  no-op for window-manager bindings.

rawclip 0.2.0 — 2026-09-26
==========================

Sensitive clipboard interoperability
------------------------------------

* Interpret `x-kde-passwordManagerHint` as a value-bearing selection target:
  request the hint before text, reject the exact value `secret`, and allow a
  readable non-secret value to proceed through normal capture policy.
* Fail closed when an owner advertises the sensitive target but cannot return a
  bounded, valid 8-bit hint value; secret or malformed offers receive no text
  request.
* Bind selection replies and incoming INCR chunks to the XFixes owner admitted
  for the transfer, and abandon pending receive state when ownership disappears.
* Reject direct selection properties that exceed the admitted read bound rather
  than accepting a truncated clipboard value.
* Document direct rawpass 1.1.0 interoperability: rawpass emits the `secret`
  hint, so default rawclip policy excludes its password and OTP clipboard data
  without application-name rules.

Testing
-------

* Extend the Xvfb owner/requestor harness with exact-secret, non-secret, and
  broken-hint cases and assert that rejected offers never receive a text
  conversion request.

rawclip 0.1.0 — 2026-09-26
==========================

Architecture
------------

* Introduce `rawclipd` as the sole X11 selection/history authority and
  `rawclip` as a local control and rawmenu frontend.
* Keep history memory-only; no persistence or global hotkey authority is
  included in the initial release.
* Use a versioned, bounded Unix-socket protocol under a validated
  `XDG_RUNTIME_DIR` and authenticate clients with Linux peer credentials.

X11
---

* Monitor CLIPBOARD and optional PRIMARY ownership changes through XFixes.
* Inspect TARGETS before requesting text and reject offers advertising
  `x-kde-passwordManagerHint` when sensitive filtering is enabled.
* Apply WM_CLASS/title/executable exclusion rules before requesting clipboard
  payload bytes.
* Capture UTF8_STRING and legacy STRING, normalize history to validated UTF-8,
  and support bounded incoming INCR transfers.
* Replay history as an ICCCM selection owner serving TARGETS, TIMESTAMP,
  UTF8_STRING and representable STRING data.

Policy and UI
-------------

* Add strict libconfig layering with bounded history, selection policy,
  sensitive-hint policy, rawmenu command/prompt, and exclusion rules.
* Add list/show/select/delete/clear/pause/resume/toggle/status/reload commands.
* Integrate the chooser by executing `rawmenu select -p PROMPT` directly over
  pipes with stable entry IDs and sanitized previews.

Testing
-------

* Add standalone history, IPC and text-encoding tests.
* Add headless CLI and configuration-rejection tests.
* Add an Xvfb-backed X11 owner/requestor harness covering sensitive-hint
  refusal before payload transfer, source exclusion, ordinary capture, replay,
  pause/resume, deletion, clearing and reload.
