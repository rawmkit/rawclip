#!/bin/sh
set -eu

bin_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
rawclipd="$bin_dir/$(basename -- "$1")"
state=$(mktemp -d "${TMPDIR:-/tmp}/rawclip-config-test.XXXXXX")
trap 'rm -rf "$state"' EXIT HUP INT TERM

check_bad()
{
    name=$1
    content=$2
    printf '%s\n' "$content" >"$state/$name.conf"
    if "$rawclipd" -C -c "$state/$name.conf" >/dev/null 2>&1; then
        echo "invalid config accepted: $name" >&2
        exit 1
    fi
}

check_bad unknown_top 'bogus = true;'
check_bad unknown_nested 'history = { max_entries = 8; surprise = true; };'
check_bad wrong_type 'history = { max_entries = "eight"; };'
check_bad no_selection 'selection = { clipboard = false; primary = false; };'
check_bad empty_rule 'exclude = ( {} );'
check_bad unknown_rule 'exclude = ( { process = "foo"; } );'
check_bad empty_menu 'menu = { command = ""; };'

echo 'rawclip config rejection tests: ok'
