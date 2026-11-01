#!/bin/sh
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
sh "$HERE/Converter.sh" "$@"
RESULT=$?
if [ "$RESULT" -ne 0 ]; then
    printf '\nPress Return to close.'
    read -r REPLY
fi
exit "$RESULT"
