#!/bin/sh

set -eu
umask 077

DATA_DIR=${GUPPY_RESTORE_DATA_DIR:-/usr/data}
BACKUP_DIR=${GUPPY_RESTORE_BACKUP_DIR:-$DATA_DIR/guppyify-backup}
ARCHIVE=${GUPPY_RESTORE_ARCHIVE:-$BACKUP_DIR/guppyscreen-original.tar.gz}
APP_DIR=$DATA_DIR/guppyscreen
SERVICE=${GUPPY_RESTORE_SERVICE:-/etc/init.d/S99guppyscreen}
STAMP=$(date +%Y%m%d-%H%M%S)-$$
WORK_DIR=$DATA_DIR/.guppyscreen-restore-$STAMP
SAVED_APP=$DATA_DIR/.guppyscreen-current-$STAMP

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT
trap 'cleanup; exit 1' HUP INT TERM

if [ ! -f "$ARCHIVE" ]; then
    echo "Не найдена резервная копия: $ARCHIVE" >&2
    echo "Предыдущая версия сохраняется при установке поверх существующего VibeScreen." >&2
    exit 1
fi

if ! tar tzf "$ARCHIVE" 2>/dev/null | grep -qx 'guppyscreen/guppyscreen'; then
    echo "Резервная копия повреждена или не содержит VibeScreen: $ARCHIVE" >&2
    exit 1
fi

mkdir -p "$BACKUP_DIR" "$WORK_DIR"
if ! tar xzf "$ARCHIVE" -C "$WORK_DIR"; then
    echo "Не удалось распаковать резервную копию. Текущая установка не изменена." >&2
    exit 1
fi

RESTORE_APP=$WORK_DIR/guppyscreen
if [ ! -x "$RESTORE_APP/guppyscreen" ]; then
    echo "В резервной копии нет исполняемой программы. Текущая установка не изменена." >&2
    exit 1
fi

# Keep the updater from this repository rather than restoring an older one.
if [ -f "$APP_DIR/update.sh" ]; then
    cp -p "$APP_DIR/update.sh" "$RESTORE_APP/update.sh"
fi

if [ -d "$APP_DIR" ]; then
    CURRENT_ARCHIVE=$BACKUP_DIR/guppyscreen-before-restore-$STAMP.tar.gz
    if ! tar czf "$CURRENT_ARCHIVE" -C "$DATA_DIR" guppyscreen; then
        rm -f "$CURRENT_ARCHIVE"
        echo "Не удалось сохранить текущую версию. Восстановление отменено." >&2
        exit 1
    fi
    if ! tar tzf "$CURRENT_ARCHIVE" 2>/dev/null | grep -qx 'guppyscreen/guppyscreen'; then
        rm -f "$CURRENT_ARCHIVE"
        echo "Не удалось проверить сохранение текущей версии. Восстановление отменено." >&2
        exit 1
    fi
fi

printf '%s\n' "Останавливаю экран и возвращаю предыдущую версию VibeScreen..."
if [ -x "$SERVICE" ]; then
    "$SERVICE" stop >/dev/null 2>&1 || true
else
    killall -q guppyscreen 2>/dev/null || true
fi

if [ -d "$APP_DIR" ]; then
    mv "$APP_DIR" "$SAVED_APP"
fi
if ! mv "$RESTORE_APP" "$APP_DIR"; then
    if [ -d "$SAVED_APP" ]; then mv "$SAVED_APP" "$APP_DIR"; fi
    echo "Не удалось установить восстановленную версию. Текущая версия сохранена." >&2
    exit 1
fi
rm -rf "$SAVED_APP"

if [ -x "$SERVICE" ]; then
    if ! "$SERVICE" restart >/dev/null 2>&1; then
        echo "Версия восстановлена, но экран не запустился автоматически." >&2
        echo "Предыдущая версия установлена в $APP_DIR" >&2
        exit 1
    fi
fi

echo "Готово. Предыдущая версия VibeScreen восстановлена."
