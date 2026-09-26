#!/bin/sh
set -eu

rawclip_dir=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)
rawclip="$rawclip_dir/$(basename -- "$1")"
rawclipd_dir=$(CDPATH= cd -- "$(dirname -- "$2")" && pwd)
rawclipd="$rawclipd_dir/$(basename -- "$2")"
config=$3
version=$4

[ "$($rawclip -v)" = "rawclip-$version" ]
[ "$($rawclipd -v)" = "rawclipd-$version" ]
"$rawclipd" -C -c "$config"

if "$rawclip" nonsense >/dev/null 2>&1; then
    echo 'unknown command succeeded' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclip" menu bogus >/dev/null 2>&1; then
    echo 'invalid menu action succeeded' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclip" show >/dev/null 2>&1; then
    echo 'missing show id succeeded' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclipd" -C >/dev/null 2>&1; then
    echo 'config check without path succeeded' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi

echo 'rawclip CLI tests: ok'
