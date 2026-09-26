#!/bin/sh
set -eu

rawclipd_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
rawclipd="$rawclipd_dir/$(basename -- "$1")"
rawclip_dir=$(CDPATH= cd -- "$(dirname -- "$2")" && pwd)
rawclip="$rawclip_dir/$(basename -- "$2")"
helper_dir=$(CDPATH= cd -- "$(dirname -- "$3")" && pwd)
helper="$helper_dir/$(basename -- "$3")"
ipc_probe_dir=$(CDPATH= cd -- "$(dirname -- "$4")" && pwd)
ipc_probe="$ipc_probe_dir/$(basename -- "$4")"

state=$(mktemp -d "${TMPDIR:-/tmp}/rawclip-test.XXXXXX")
cleanup()
{
    set +e
    [ -n "${owner_pid:-}" ] && kill "$owner_pid" 2>/dev/null
    [ -n "${daemon_pid:-}" ] && kill "$daemon_pid" 2>/dev/null
    [ -n "${xvfb_pid:-}" ] && kill "$xvfb_pid" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$state"
}
trap cleanup EXIT HUP INT TERM

export XDG_RUNTIME_DIR="$state/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export DISPLAY=:$((100 + ($$ % 500)))
Xvfb "$DISPLAY" -screen 0 800x600x24 -nolisten tcp >"$state/xvfb.log" 2>&1 &
xvfb_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$helper" ping >/dev/null 2>&1 && break || sleep 0.05
done
"$helper" ping

# Client and daemon control authority require a private runtime directory.
mkdir -m 770 "$state/unsafe-runtime"
if XDG_RUNTIME_DIR="$state/unsafe-runtime" "$rawclip" health >"$state/unsafe.out" 2>"$state/unsafe.err"; then
    echo 'rawclip accepted a group-writable runtime directory' >&2
    exit 1
fi
grep -q 'Operation not permitted' "$state/unsafe.err"

display_key=$(printf '%s' "$DISPLAY" | sed 's/[^A-Za-z0-9_-]/_/g')
lock_path="$XDG_RUNTIME_DIR/rawclip-$display_key.sock.lock"
: >"$lock_path"
chmod 0666 "$lock_path"

cat >"$state/rawclip.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = false; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; preview_chars = 3; };
exclude = ( { class = "ExcludedApp"; } );
EOF2
cat >"$state/fake-menu" <<'EOF2'
#!/bin/sh
set -eu
[ "$#" -eq 4 ]
[ "$1" = "-M" ]
[ "$2" = "select" ]
[ "$3" = "-p" ]
case "$4" in
    clipboard|"clipboard delete"|"clipboard pin") ;;
    *) exit 2 ;;
esac
case "${RAWCLIP_TEST_MENU_BEHAVIOR:-select}" in
    cancel) exit 1 ;;
    fail) exit 42 ;;
    empty) cat >/dev/null; exit 0 ;;
    select) ;;
    *) exit 2 ;;
esac
IFS= read -r first || exit 1
[ -z "${RAWCLIP_TEST_MENU_CAPTURE:-}" ] || printf '%s\n' "$first" >"$RAWCLIP_TEST_MENU_CAPTURE"
printf '%s\n' "$first"
EOF2
chmod +x "$state/fake-menu"

"$rawclipd" -c "$state/rawclip.conf" >"$state/daemon.out" 2>"$state/daemon.err" &
daemon_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$rawclip" status >/dev/null 2>&1 && break || sleep 0.05
done
"$rawclip" status | grep -q 'paused=0 entries=0'
[ "$("$rawclip" health)" = 'ok' ]
[ "$(stat -c '%a' "$lock_path")" = '600' ]
if "$rawclipd" -c "$state/rawclip.conf" >"$state/second-daemon.out" 2>"$state/second-daemon.err"; then
    echo 'second rawclipd instance unexpectedly started' >&2
    exit 1
fi
[ "$("$rawclip" health)" = 'ok' ]
"$ipc_probe" partial-frame >"$state/partial.out" 2>"$state/partial.err" &
partial_pid=$!
sleep 0.1
wait "$partial_pid"
[ "$("$rawclip" health)" = 'ok' ]
"$rawclip" -c "$state/rawclip.conf" menu >"$state/empty-menu.out" 2>"$state/empty-menu.err"
[ ! -s "$state/empty-menu.out" ]
grep -q '^rawclip: history is empty$' "$state/empty-menu.err"

# Sensitive offers are rejected after TARGETS and before payload bytes.
"$helper" offer 'secret-value' --secret >"$state/secret-owner.out" &
owner_pid=$!
sleep 0.15
[ -z "$("$rawclip" list)" ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=0$' "$state/secret-owner.out"
grep -q '^hint_requests=1$' "$state/secret-owner.out"

# A non-secret value on the convention target is not itself sensitive.
"$helper" offer 'hinted-public-value' --hint public >"$state/public-hint-owner.out" &
owner_pid=$!
sleep 0.15
"$rawclip" list | grep -q 'hinted-public-value'
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=1$' "$state/public-hint-owner.out"
grep -q '^hint_requests=1$' "$state/public-hint-owner.out"
"$rawclip" clear

# An advertised hint which cannot be retrieved fails closed before text.
"$helper" offer 'broken-hint-value' --broken-hint >"$state/broken-hint-owner.out" &
owner_pid=$!
sleep 0.15
[ -z "$("$rawclip" list)" ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=0$' "$state/broken-hint-owner.out"
grep -q '^hint_requests=1$' "$state/broken-hint-owner.out"

# Excluded owners are rejected before even TARGETS/text transfer reaches history.
"$helper" offer 'excluded-value' --class ExcludedApp >"$state/excluded-owner.out" &
owner_pid=$!
sleep 0.15
[ -z "$("$rawclip" list)" ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=0$' "$state/excluded-owner.out"

# A transfer admitted under the old policy epoch must not complete after a
# reload tightens owner exclusion. Wait until the text request is in flight so
# the regression specifically crosses the reload boundary.
cp "$state/rawclip.conf" "$state/rawclip.conf.before-epoch"
rm -f "$state/epoch-requested"
"$helper" offer 'stale-policy-value' --class EpochApp --delay-text-ms 500 \
    --text-request-marker "$state/epoch-requested" >"$state/epoch-owner.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    [ -f "$state/epoch-requested" ] && break || sleep 0.05
done
[ -f "$state/epoch-requested" ]
cat >"$state/rawclip.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = false; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; preview_chars = 3; };
exclude = ( { class = "EpochApp"; } );
EOF2
"$rawclip" reload
sleep 0.6
! "$rawclip" list | grep -q 'stale-policy-value'
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
mv "$state/rawclip.conf.before-epoch" "$state/rawclip.conf"
"$rawclip" reload

# Malformed INCR framing is rejected rather than treated as a valid text
# transfer or terminator.
"$helper" offer 'bad-incr-header-value' --bad-incr-header >"$state/bad-incr-header.out" &
owner_pid=$!
sleep 0.15
! "$rawclip" list | grep -q 'bad-incr-header-value'
"$rawclip" status | grep -q 'receive_pending=0'
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$helper" offer-size-bad-end 400000 >"$state/bad-incr-end.out" &
owner_pid=$!
sleep 0.3
[ -z "$("$rawclip" list)" ]
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=

# Ordinary text is captured once, deduplicated, and replayable.
"$helper" offer 'alpha-value' >"$state/alpha-owner.out" &
owner_pid=$!
sleep 0.15
line=$("$rawclip" list)
printf '%s\n' "$line" | grep -q 'alpha-value'
id=${line%%	*}
"$ipc_probe" abort-list
sleep 0.05
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=

"$rawclip" show "$id" | grep -q '^alpha-value$'
RAWCLIP_TEST_MENU_BEHAVIOR=cancel "$rawclip" -c "$state/rawclip.conf" menu
if RAWCLIP_TEST_MENU_BEHAVIOR=fail "$rawclip" -c "$state/rawclip.conf" menu >"$state/menu-fail.out" 2>"$state/menu-fail.err"; then
    echo 'failing menu frontend was reported as success' >&2
    exit 1
fi
grep -q '^rawclip: menu: Input/output error$' "$state/menu-fail.err"
if RAWCLIP_TEST_MENU_BEHAVIOR=empty "$rawclip" -c "$state/rawclip.conf" menu >"$state/menu-empty.out" 2>"$state/menu-empty.err"; then
    echo 'empty successful menu output was accepted' >&2
    exit 1
fi
grep -q '^rawclip: menu: Protocol error$' "$state/menu-empty.err"
RAWCLIP_TEST_MENU_CAPTURE="$state/menu-delete.input" "$rawclip" -c "$state/rawclip.conf" menu delete
[ -z "$("$rawclip" list)" ]
[ "$(cat "$state/menu-delete.input")" = "$(printf '%s\t%s' "$id" '- alp…')" ]

"$helper" offer 'alpha-value' >"$state/alpha-reowner.out" &
owner_pid=$!
sleep 0.15
line=$("$rawclip" list)
id=${line%%	*}
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
RAWCLIP_TEST_MENU_CAPTURE="$state/menu-select.input" "$rawclip" -c "$state/rawclip.conf" menu
[ "$("$helper" paste)" = 'alpha-value' ]
[ "$(cat "$state/menu-select.input")" = "$(printf '%s\t%s' "$id" '- alp…')" ]

# Pinning protects history from automatic cleanup while preserving explicit controls.
"$rawclip" pin "$id"
"$rawclip" status | grep -q 'pinned=1'
printf '%s\n' "$("$rawclip" list)" | grep -F "$(printf '%s\t%s' "$id" '* alpha-value')"
RAWCLIP_TEST_MENU_CAPTURE="$state/menu-pin.input" "$rawclip" -c "$state/rawclip.conf" menu toggle-pin
[ "$(cat "$state/menu-pin.input")" = "$(printf '%s\t%s' "$id" '* alp…')" ]
"$rawclip" status | grep -q 'pinned=0'
RAWCLIP_TEST_MENU_CAPTURE="$state/menu-repin.input" "$rawclip" -c "$state/rawclip.conf" menu toggle-pin
[ "$(cat "$state/menu-repin.input")" = "$(printf '%s\t%s' "$id" '- alp…')" ]
"$rawclip" status | grep -q 'pinned=1'

"$helper" offer 'temporary-value' >"$state/temporary-owner.out" &
owner_pid=$!
sleep 0.15
"$rawclip" list | grep -q 'temporary-value'
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear unpinned
line=$("$rawclip" list)
printf '%s\n' "$line" | grep -q 'alpha-value'
! printf '%s\n' "$line" | grep -q 'temporary-value'
"$rawclip" unpin "$id"
"$rawclip" status | grep -q 'pinned=0'

"$rawclip" clear
"$helper" offer 'éééé' >"$state/utf8-owner.out" &
owner_pid=$!
sleep 0.15
line=$("$rawclip" list)
id=${line%%	*}
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
RAWCLIP_TEST_MENU_CAPTURE="$state/menu-utf8.input" "$rawclip" -c "$state/rawclip.conf" menu
[ "$(cat "$state/menu-utf8.input")" = "$(printf '%s\t%s' "$id" '- ééé…')" ]
"$rawclip" clear

"$helper" offer 'alpha-value' >"$state/alpha-final-owner.out" &
owner_pid=$!
sleep 0.15
line=$("$rawclip" list)
id=${line%%	*}
kill "$owner_pid"
wait "$owner_pid"
owner_pid=

"$rawclip" pause
"$rawclip" status | grep -q 'paused=1'
"$helper" offer 'paused-value' >"$state/paused-owner.out" &
owner_pid=$!
sleep 0.15
! "$rawclip" list | grep -q 'paused-value'
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" resume
[ "$("$rawclip" health)" = 'ok' ]

cp "$state/rawclip.conf" "$state/rawclip.conf.good"
printf '%s\n' 'bogus = true;' >"$state/rawclip.conf"
if "$rawclip" reload >/dev/null 2>&1; then
    echo 'invalid configuration reload succeeded' >&2
    exit 1
fi
[ "$("$rawclip" health)" = 'ok' ]
mv "$state/rawclip.conf.good" "$state/rawclip.conf"
"$rawclip" reload

"$rawclip" delete "$id"
[ -z "$("$rawclip" list)" ]

i=0
while [ "$i" -lt 25 ]; do
    "$helper" offer "churn-$i" >"$state/churn-$i.out" &
    churn_pid=$!
    sleep 0.01
    kill "$churn_pid" 2>/dev/null || true
    wait "$churn_pid" 2>/dev/null || true
    i=$((i + 1))
done
[ "$("$rawclip" health)" = 'ok' ]
"$helper" offer 'churn-final' >"$state/churn-final.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$rawclip" list | grep -q 'churn-final' && break || sleep 0.05
done
"$rawclip" list | grep -q 'churn-final'
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear
"$rawclip" status | grep -q 'receive_pending=0'

# A fully pinned bounded history drops new captures without killing the daemon.
cp "$state/rawclip.conf" "$state/rawclip.conf.before-pins"
cat >"$state/rawclip.conf" <<EOF2
history = { max_entries = 2; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = false; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; preview_chars = 3; };
exclude = ();
EOF2
"$rawclip" reload
for value in pinned-one pinned-two; do
    "$helper" offer "$value" >"$state/$value.out" &
    owner_pid=$!
    sleep 0.15
    line=$("$rawclip" list)
    pid_id=$(printf '%s\n' "$line" | grep "$value" | cut -f1)
    "$rawclip" pin "$pid_id"
    kill "$owner_pid"
    wait "$owner_pid"
    owner_pid=
done
"$rawclip" status | grep -q 'entries=2 pinned=2'
"$helper" offer 'must-drop' >"$state/must-drop.out" &
owner_pid=$!
sleep 0.15
! "$rawclip" list | grep -q 'must-drop'
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
# Reload must fail rather than evict pinned state to satisfy tighter limits.
cat >"$state/rawclip.conf" <<EOF2
history = { max_entries = 1; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = false; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; preview_chars = 3; };
exclude = ();
EOF2
if "$rawclip" reload >/dev/null 2>&1; then
    echo 'reload evicted pinned entries to satisfy tighter capacity' >&2
    exit 1
fi
"$rawclip" status | grep -q 'entries=2 pinned=2'
[ "$("$rawclip" health)" = 'ok' ]
"$rawclip" clear
mv "$state/rawclip.conf.before-pins" "$state/rawclip.conf"
"$rawclip" reload

# Large clipboard values cross both sides of the ICCCM INCR boundary.
large_bytes=400000
dd if=/dev/zero bs="$large_bytes" count=1 2>/dev/null | tr '\000' L >"$state/large.expected"
"$helper" offer-size "$large_bytes" >"$state/large-owner.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30; do
    line=$("$rawclip" list)
    [ -n "$line" ] && break || sleep 0.05
done
[ -n "${line:-}" ]
id=${line%%	*}
"$rawclip" show "$id" >"$state/large.show"
cmp "$state/large.expected" "$state/large.show"
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=1$' "$state/large-owner.out"

"$rawclip" -c "$state/rawclip.conf" menu
"$helper" probe-owner
"$helper" paste-string >"$state/large.paste.string"
cmp "$state/large.expected" "$state/large.paste.string"
"$helper" paste >"$state/large.paste.1" &
paste1=$!
"$helper" paste >"$state/large.paste.2" &
paste2=$!
wait "$paste1"
wait "$paste2"
cmp "$state/large.expected" "$state/large.paste.1"
cmp "$state/large.expected" "$state/large.paste.2"

# Aborted INCR requestors must not consume the bounded transfer table.
i=0
while [ "$i" -lt 20 ]; do
    "$helper" paste-abort >/dev/null
    i=$((i + 1))
done
"$helper" paste >"$state/large.paste.after-abort"
cmp "$state/large.expected" "$state/large.paste.after-abort"

"$rawclip" clear
"$rawclip" reload

# PRIMARY/CLIPBOARD synchronization is explicit policy. Restart with both
# selections monitored and bidirectional synchronization enabled.
kill "$daemon_pid"
wait "$daemon_pid"
daemon_pid=
cat >"$state/rawclip-sync.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = true; synchronize = "bidirectional"; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ();
EOF2
"$rawclipd" -c "$state/rawclip-sync.conf" >"$state/sync-daemon.out" 2>"$state/sync-daemon.err" &
daemon_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$rawclip" status >/dev/null 2>&1 && break || sleep 0.05
done
"$rawclip" status | grep -q 'clipboard=1 primary=1 synchronize=bidirectional'

# Protected-full admission must not leak a dropped PRIMARY value through sync.
cat >"$state/rawclip-sync.conf" <<EOF2
history = { max_entries = 1; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = true; synchronize = "bidirectional"; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ();
EOF2
"$rawclip" reload
"$helper" offer 'pinned-anchor' >"$state/pinned-anchor.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    line=$("$rawclip" list)
    printf '%s\n' "$line" | grep -q 'pinned-anchor' && break || sleep 0.05
done
id=${line%%	*}
"$rawclip" pin "$id"
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$helper" offer 'drop-primary' --primary >"$state/drop-primary.out" &
owner_pid=$!
sleep 0.15
! "$rawclip" list | grep -q 'drop-primary'
if "$helper" paste 2>/dev/null | grep -q '^drop-primary$'; then
    echo 'dropped PRIMARY value was synchronized to CLIPBOARD' >&2
    exit 1
fi
[ "$("$rawclip" health)" = 'ok' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear
cat >"$state/rawclip-sync.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = true; synchronize = "bidirectional"; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ();
EOF2
"$rawclip" reload

# PRIMARY admission mirrors ordinary text to CLIPBOARD exactly once.
"$helper" offer 'from-primary' --primary >"$state/primary-owner.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    line=$("$rawclip" list)
    printf '%s\n' "$line" | grep -q 'from-primary' && break || sleep 0.05
done
printf '%s\n' "$line" | grep -q 'from-primary'
[ "$(printf '%s\n' "$line" | wc -l)" -eq 1 ]
id=${line%%	*}
[ "$("$helper" paste)" = 'from-primary' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
# Explicit history replay owns CLIPBOARD and follows synchronization policy.
"$rawclip" select "$id"
[ "$("$helper" paste-primary)" = 'from-primary' ]
"$rawclip" clear

# CLIPBOARD admission mirrors ordinary text to PRIMARY exactly once.
"$helper" offer 'from-clipboard' >"$state/clipboard-owner.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    line=$("$rawclip" list)
    printf '%s\n' "$line" | grep -q 'from-clipboard' && break || sleep 0.05
done
printf '%s\n' "$line" | grep -q 'from-clipboard'
[ "$(printf '%s\n' "$line" | wc -l)" -eq 1 ]
[ "$("$helper" paste-primary)" = 'from-clipboard' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear

# A secret PRIMARY offer is neither admitted nor mirrored to CLIPBOARD.
"$helper" offer 'primary-secret' --secret --primary >"$state/primary-secret.out" &
owner_pid=$!
sleep 0.15
[ -z "$("$rawclip" list)" ]
if "$helper" paste >/dev/null 2>&1; then
    echo 'sensitive PRIMARY was synchronized to CLIPBOARD' >&2
    exit 1
fi
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=0$' "$state/primary-secret.out"

# A secret CLIPBOARD offer is neither admitted nor mirrored to PRIMARY.
"$helper" offer 'clipboard-secret' --secret >"$state/clipboard-secret.out" &
owner_pid=$!
sleep 0.15
[ -z "$("$rawclip" list)" ]
if "$helper" paste-primary >/dev/null 2>&1; then
    echo 'sensitive CLIPBOARD was synchronized to PRIMARY' >&2
    exit 1
fi
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
grep -q '^text_requests=0$' "$state/clipboard-secret.out"

# Tightening policy releases synchronization-owned selections on reload.
"$helper" offer 'reload-source' >"$state/reload-owner.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$helper" paste-primary 2>/dev/null | grep -q '^reload-source$' && break || sleep 0.05
done
[ "$("$helper" paste-primary)" = 'reload-source' ]
line=$("$rawclip" list)
id=${line%%	*}
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
# Make CLIPBOARD explicit replay authority; PRIMARY remains synchronization-owned.
"$rawclip" select "$id"
cat >"$state/rawclip-sync.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = true; synchronize = "none"; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ();
EOF2
"$rawclip" reload
"$rawclip" status | grep -q 'clipboard=1 primary=1 synchronize=none'
if "$helper" paste-primary >/dev/null 2>&1; then
    echo 'reload retained synchronization-owned PRIMARY' >&2
    exit 1
fi
[ "$("$helper" paste)" = 'reload-source' ]

# PRIMARY monitoring remains useful without synchronization.
"$rawclip" clear
"$helper" offer 'primary-history-only' --primary >"$state/primary-only.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    line=$("$rawclip" list)
    printf '%s\n' "$line" | grep -q 'primary-history-only' && break || sleep 0.05
done
printf '%s\n' "$line" | grep -q 'primary-history-only'
if "$helper" paste >/dev/null 2>&1; then
    echo 'PRIMARY monitoring synchronized while policy was none' >&2
    exit 1
fi
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear

# Directional policy does not imply the reverse direction.
cat >"$state/rawclip-sync.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = true; synchronize = "primary-to-clipboard"; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ();
EOF2
"$rawclip" reload
"$helper" offer 'one-way-clipboard' >"$state/one-way-clipboard.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    line=$("$rawclip" list)
    printf '%s\n' "$line" | grep -q 'one-way-clipboard' && break || sleep 0.05
done
if "$helper" paste-primary >/dev/null 2>&1; then
    echo 'primary-to-clipboard policy mirrored CLIPBOARD in reverse' >&2
    exit 1
fi
kill "$owner_pid"
wait "$owner_pid"
owner_pid=
"$rawclip" clear
"$helper" offer 'one-way-primary' --primary >"$state/one-way-primary.out" &
owner_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$helper" paste 2>/dev/null | grep -q '^one-way-primary$' && break || sleep 0.05
done
[ "$("$helper" paste)" = 'one-way-primary' ]
kill "$owner_pid"
wait "$owner_pid"
owner_pid=

echo 'rawclip integration tests: ok'
