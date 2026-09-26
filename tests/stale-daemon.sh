#!/bin/sh
set -eu

rawclip_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
rawclip="$rawclip_dir/$(basename -- "$1")"
stale_dir=$(CDPATH= cd -- "$(dirname -- "$2")" && pwd)
stale="$stale_dir/$(basename -- "$2")"

state=$(mktemp -d "${TMPDIR:-/tmp}/rawclip-stale-test.XXXXXX")
cleanup()
{
    set +e
    [ -n "${stale_pid:-}" ] && kill "$stale_pid" 2>/dev/null
    wait 2>/dev/null
    rm -rf "$state"
}
trap cleanup EXIT HUP INT TERM

export XDG_RUNTIME_DIR="$state/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export DISPLAY=:4242

"$stale" >"$state/stale.out" 2>"$state/stale.err" &
stale_pid=$!
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
    grep -q '^ready$' "$state/stale.out" 2>/dev/null && break || sleep 0.05
done
grep -q '^ready$' "$state/stale.out"

if "$rawclip" health >"$state/client.out" 2>"$state/client.err"; then
    echo 'rawclip accepted a stale incompatible daemon' >&2
    exit 1
fi
grep -q '^rawclip: health: control connection reset; rawclipd may be stale or incompatible; restart rawclipd and retry$' "$state/client.err"

wait "$stale_pid"
stale_pid=

echo 'stale daemon diagnostic tests: ok'
