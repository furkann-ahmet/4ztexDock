#!/usr/bin/env bash
set -u

SCREEN="${ZTEX_TOOLBAR_SCREEN:-DP-3}"
POLL_INTERVAL="${ZTEX_TOOLBAR_POLL_INTERVAL:-0.5}"

signature() {
    find src style -type f -printf '%p %T@\n'
    printf '4ztexDock.pro %s\n' "$(stat -c '%Y' 4ztexDock.pro 2>/dev/null || true)"
}

restart_toolbar() {
    echo "[dev] building..."
    if ! make; then
        echo "[dev] build failed; keeping current toolbar state"
        return
    fi

    echo "[dev] restarting on ${SCREEN}..."
    pkill -x 4ztexDock 2>/dev/null || true
    ./4ztexDock --screen "${SCREEN}" &
}

echo "[dev] watching src/, style/, and 4ztexDock.pro"
echo "[dev] screen: ${SCREEN}"
echo "[dev] press Ctrl+C to stop"

last_signature="$(signature)"
restart_toolbar

while true; do
    sleep "${POLL_INTERVAL}"
    current_signature="$(signature)"

    if [[ "${current_signature}" != "${last_signature}" ]]; then
        last_signature="${current_signature}"
        restart_toolbar
    fi
done
