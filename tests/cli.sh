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

for bad in '+1' ' 1' '0' '18446744073709551616'; do
    if "$rawclip" show "$bad" >/dev/null 2>&1; then
        echo "invalid entry id succeeded: $bad" >&2
        exit 1
    else
        [ "$?" -eq 2 ]
    fi
done
if "$rawclip" -v trailing >/dev/null 2>&1; then
    echo 'version mode accepted trailing arguments' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclip" -c "$config" -v >/dev/null 2>&1; then
    echo 'rawclip version mode accepted config policy' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclipd" -c "$config" -v >/dev/null 2>&1; then
    echo 'rawclipd version mode accepted config policy' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi

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
if "$rawclip" pin >/dev/null 2>&1; then
    echo 'missing pin id succeeded' >&2
    exit 1
else
    [ "$?" -eq 2 ]
fi
if "$rawclip" clear bogus >/dev/null 2>&1; then
    echo 'invalid clear mode succeeded' >&2
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
