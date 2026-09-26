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
