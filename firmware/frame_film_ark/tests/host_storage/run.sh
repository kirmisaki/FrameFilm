#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin=$(mktemp /tmp/framefilm-ark-storage-test.XXXXXX)
trap 'rm -f "$bin"' EXIT HUP INT TERM
for feature in DISABLED CONFIG_ARK_EPD_ABC_PREVIEW CONFIG_ARK_EPD_ABC_DIAG; do
  cc -std=c11 -D_GNU_SOURCE -D"$feature=1" -Wall -Wextra -Wno-unused-function \
    -I"$here/stubs" \
    -I"$here/../../components/film_sys/inc" \
    -I"$here/../../components/film_service/inc" \
    "$here/test_service_file.c" -o "$bin"
  printf '%s: ' "$feature"
  "$bin"
done
