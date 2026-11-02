#!/bin/sh

set -eu

BETA_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
INSTALLER=$BETA_DIR/installer.sh
ARCHIVE=$BETA_DIR/guppyscreen-0.0.21.beta_test-mips-material.tar.gz
CHECKSUM=$ARCHIVE.sha256

if [ "$(id -u)" -ne 0 ]; then
    echo "Запустите этот скрипт из root shell на принтере." >&2
    exit 1
fi

if [ ! -f "$INSTALLER" ]; then
    echo "Не найден installer.sh рядом с Beta-пакетом." >&2
    exit 1
fi

if [ ! -f "$ARCHIVE" ]; then
    echo "Не найден Beta-архив: $ARCHIVE" >&2
    exit 1
fi

if [ ! -f "$CHECKSUM" ] || ! python3 - "$ARCHIVE" "$CHECKSUM" <<'PY'
import hashlib
import pathlib
import sys

archive = pathlib.Path(sys.argv[1])
expected = pathlib.Path(sys.argv[2]).read_text(encoding="ascii").split()[0]
digest = hashlib.sha256()
with archive.open("rb") as source:
    for block in iter(lambda: source.read(1024 * 1024), b""):
        digest.update(block)
if digest.hexdigest() != expected:
    raise SystemExit(1)
PY
then
    echo "Не удалось проверить SHA-256 Beta-архива. Установка отменена." >&2
    exit 1
fi

exec sh "$INSTALLER" --archive "$ARCHIVE"
