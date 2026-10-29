#!/usr/bin/env bash

set -u
umask 077
export PATH="${HOME:-}/.local/bin:${HOME:-}/.cargo/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin:${PATH:-}"

RUNDIR="/dev/shm/afterflow-$(id -un)"
if [ ! -d /dev/shm ] || [ ! -w /dev/shm ]; then
    RUNDIR="/tmp/afterflow-$(id -un)"
fi
TRIGGER="$RUNDIR/open-kitty"
LAUNCHER="$RUNDIR/terminal.sh"
LOG="$RUNDIR/watch.log"
ENABLED="$RUNDIR/terminal-enabled"
LEGACY="/tmp/afterflow-$(id -un)"

mkdir -p "$RUNDIR" 2>/dev/null || true
chmod 700 "$RUNDIR" 2>/dev/null || true

# Old versions kept the terminal log on /tmp, which can be disk-backed.
# Remove those copies when the volatile runtime directory is available.
if [ "$RUNDIR" != "$LEGACY" ]; then
    rm -f "$LEGACY/terminal.log" "$LEGACY/watch.log" "$LEGACY/kitty.log" 2>/dev/null || true
fi

log_line() {
    local line
    line="$(date '+%H:%M:%S') $*"
    printf '%s\n' "$line" >>"$LOG" 2>/dev/null || true
    printf '%s\n' "$line" >&2
}

import_session_env() {
    local uid
    uid="$(id -u)"
    if [ -z "${HOME:-}" ]; then
        HOME="$(getent passwd "$uid" | cut -d: -f6)"
        export HOME
    fi
    [ -n "${USER:-}" ] || export USER="$(id -un)"
    [ -n "${LOGNAME:-}" ] || export LOGNAME="$USER"
    if [ -z "${XDG_RUNTIME_DIR:-}" ] || [ ! -d "${XDG_RUNTIME_DIR:-}" ]; then
        export XDG_RUNTIME_DIR="/run/user/$uid"
    fi

    local key value
    if command -v systemctl >/dev/null 2>&1; then
        while IFS='=' read -r key value; do
            case "$key" in
                WAYLAND_DISPLAY|HYPRLAND_INSTANCE_SIGNATURE|XDG_RUNTIME_DIR|DISPLAY|XDG_SESSION_TYPE|XDG_CURRENT_DESKTOP|DBUS_SESSION_BUS_ADDRESS)
                    [ -n "$value" ] && export "$key=$value"
                    ;;
            esac
        done < <(timeout 2 systemctl --user show-environment 2>/dev/null)
    fi

    if [ -n "${WAYLAND_DISPLAY:-}" ]; then
        case "$WAYLAND_DISPLAY" in
            /*)
                [ -S "$WAYLAND_DISPLAY" ] || unset WAYLAND_DISPLAY
                ;;
            *)
                [ -S "${XDG_RUNTIME_DIR:-}/$WAYLAND_DISPLAY" ] || unset WAYLAND_DISPLAY
                ;;
        esac
    fi

    if [ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] && [ ! -d "${XDG_RUNTIME_DIR:-}/hypr/$HYPRLAND_INSTANCE_SIGNATURE" ]; then
        unset HYPRLAND_INSTANCE_SIGNATURE
    fi

    local comm pid entry session_found
    session_found=0
    for comm in /proc/[0-9]*/comm; do
        pid="${comm#/proc/}"
        pid="${pid%/comm}"
        [ -n "$pid" ] || continue
        [ "$(stat -c '%u' "/proc/$pid" 2>/dev/null)" = "$uid" ] || continue
        case "$(cat "$comm" 2>/dev/null)" in
            Hyprland|hyprland|sway|niri|kwin_wayland|kwin_x11|gnome-shell|plasmashell|mutter|xfwm4|openbox|wayfire|labwc|river|weston|i3|bspwm|dwm)
                ;;
            *)
                continue
                ;;
        esac
        [ -r "/proc/$pid/environ" ] || continue
        while IFS= read -r -d '' entry; do
            case "$entry" in
                WAYLAND_DISPLAY=*|HYPRLAND_INSTANCE_SIGNATURE=*|XDG_RUNTIME_DIR=*|DISPLAY=*|XDG_SESSION_TYPE=*|XDG_CURRENT_DESKTOP=*|DBUS_SESSION_BUS_ADDRESS=*)
                    export "$entry"
                    session_found=1
                    ;;
            esac
        done < "/proc/$pid/environ"
        [ "$session_found" = "1" ] && break
    done

    if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -d "${XDG_RUNTIME_DIR:-}" ]; then
        local socket
        for socket in $(ls -1t "$XDG_RUNTIME_DIR"/wayland-* 2>/dev/null); do
            [ -S "$socket" ] || continue
            export WAYLAND_DISPLAY="${socket##*/}"
            break
        done
    fi

    if [ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] && [ -d "${XDG_RUNTIME_DIR:-}/hypr" ]; then
        local instance
        for instance in $(ls -1t "$XDG_RUNTIME_DIR"/hypr/* 2>/dev/null); do
            [ -d "$instance" ] || continue
            export HYPRLAND_INSTANCE_SIGNATURE="${instance##*/}"
            break
        done
    fi

    if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ] && [ -S "${XDG_RUNTIME_DIR:-}/bus" ]; then
        export DBUS_SESSION_BUS_ADDRESS="unix:path=$XDG_RUNTIME_DIR/bus"
    fi

    if [ -z "${DISPLAY:-}" ]; then
        local xdisplay
        for xdisplay in /tmp/.X11-unix/X*; do
            [ -S "$xdisplay" ] || continue
            export DISPLAY=":${xdisplay##*X}"
            break
        done
    fi

    if [ -z "${XDG_SESSION_TYPE:-}" ] || [ "${XDG_SESSION_TYPE:-}" = "tty" ]; then
        if [ -n "${WAYLAND_DISPLAY:-}" ]; then
            export XDG_SESSION_TYPE="wayland"
        elif [ -n "${DISPLAY:-}" ]; then
            export XDG_SESSION_TYPE="x11"
        fi
    fi

    [ -n "${WAYLAND_DISPLAY:-}" ] || [ -n "${DISPLAY:-}" ]
}

ensure_rule() {
    command -v hyprctl >/dev/null 2>&1 || return 0
    hyprctl eval 'hl.window_rule({name="afterflow-terminal", match={class="^(afterflow|afterflow-nocapture)$"}, float=true, size={"1000","700"}})' >/dev/null 2>&1 || true
}

terminal_enabled() {
    [ ! -r "$ENABLED" ] || [ "$(cat "$ENABLED" 2>/dev/null)" != "0" ]
}

stamp() {
    stat -c '%i:%y:%s' "$TRIGGER" 2>/dev/null || true
}

import_session_env || true
ensure_rule
log_line "session environment: wayland=${WAYLAND_DISPLAY:-none} display=${DISPLAY:-none}"

last="$(stamp)"
if [ -n "$last" ]; then
    log_line "ignoring pre-existing trigger"
fi

while true; do
    current="$(stamp)"
    if [ -n "$current" ] && [ "$current" != "$last" ]; then
        last="$current"
        rm -f "$TRIGGER" 2>/dev/null || true
        if ! terminal_enabled; then
            log_line "terminal disabled"
            continue
        fi
        if ! import_session_env; then
            log_line "session environment unavailable"
        fi
        if ! command -v kitty >/dev/null 2>&1; then
            log_line "kitty not found on path"
            continue
        fi
        log_line "opening kitty (wayland=${WAYLAND_DISPLAY:-none} display=${DISPLAY:-none})"
        ensure_rule
        if [ -r "$LAUNCHER" ]; then
            /bin/bash "$LAUNCHER" >>"$LOG" 2>&1 || true
        else
            log_line "launcher missing: $LAUNCHER"
        fi
    fi
    sleep 0.2
done
