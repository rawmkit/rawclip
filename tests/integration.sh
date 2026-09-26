#!/bin/sh
set -eu

rawclipd_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
rawclipd="$rawclipd_dir/$(basename -- "$1")"
rawclip_dir=$(CDPATH= cd -- "$(dirname -- "$2")" && pwd)
rawclip="$rawclip_dir/$(basename -- "$2")"
helper_dir=$(CDPATH= cd -- "$(dirname -- "$3")" && pwd)
helper="$helper_dir/$(basename -- "$3")"

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

cat >"$state/rawclip.conf" <<EOF2
history = { max_entries = 8; max_entry_bytes = 524288; deduplicate = true; };
selection = { clipboard = true; primary = false; };
security = { honor_password_manager_hint = true; };
menu = { command = "$state/fake-menu"; prompt = "clipboard"; };
exclude = ( { class = "ExcludedApp"; } );
EOF2
cat >"$state/fake-menu" <<'EOF2'
#!/bin/sh
set -eu
[ "$#" -eq 4 ]
[ "$1" = "-M" ]
[ "$2" = "select" ]
[ "$3" = "-p" ]
[ "$4" = "clipboard" ]
IFS= read -r first || exit 1
printf '%s\n' "$first"
EOF2
chmod +x "$state/fake-menu"

"$rawclipd" -c "$state/rawclip.conf" >"$state/daemon.out" 2>"$state/daemon.err" &
daemon_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    "$rawclip" status >/dev/null 2>&1 && break || sleep 0.05
done
"$rawclip" status | grep -q 'paused=0 entries=0'
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

# Ordinary text is captured once, deduplicated, and replayable.
"$helper" offer 'alpha-value' >"$state/alpha-owner.out" &
owner_pid=$!
sleep 0.15
line=$("$rawclip" list)
printf '%s\n' "$line" | grep -q 'alpha-value'
id=${line%%	*}
kill "$owner_pid"
wait "$owner_pid"
owner_pid=

"$rawclip" -c "$state/rawclip.conf" menu
[ "$("$helper" paste)" = 'alpha-value' ]
"$rawclip" show "$id" | grep -q '^alpha-value$'

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

"$rawclip" delete "$id"
[ -z "$("$rawclip" list)" ]

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

echo 'rawclip integration tests: ok'
