#!/usr/bin/env bash

# RetroGC Launcher (UI version) - toggle from Main menu -> Scripts -> retrogc_ui.sh
#
# ON:  installs a hook in /media/fat/linux/user-startup.sh and starts the launcher
# OFF: removes the hook and stops the launcher (back to the stock MiSTer menu)
#
# The script-only launcher (retrogc.sh) watches the same card: only one of them can be ON.

VERSION="1.0"

BINARY="/media/fat/retrogc/retrogc-mister"
SCRIPT="/media/fat/Scripts/retrogc_ui.sh"
STARTUP="/media/fat/linux/user-startup.sh"

PIDFILE="/tmp/retrogc-mister.pid"
LOGFILE="/tmp/retrogc-mister.log"
SCRIPT_ONLY_PIDFILE="/tmp/retrogc-launcher.pid"


START_MARK="# >>> RetroGC Launcher UI >>>"
END_MARK="# <<< RetroGC Launcher UI <<<"

pid_alive() {
    local pidfile="$1" pid
    [[ -f "$pidfile" ]] || return 1
    pid="$(cat "$pidfile" 2>/dev/null)" || return 1
    [[ "$pid" =~ ^[0-9]+$ ]] || return 1
    kill -0 "$pid" 2>/dev/null
}

is_running() { pid_alive "$PIDFILE"; }

# Remove every hook block and trailing blank lines, so enable/disable cycles never grow the file.
strip_startup_hook() {
    local content
    sed -i "/$START_MARK/,/$END_MARK/d" "$STARTUP"
    content="$(cat "$STARTUP")"
    if [[ -n "$content" ]]; then
        printf '%s\n' "$content" > "$STARTUP"
    else
        : > "$STARTUP"
    fi
}

install_startup_hook() {
    mkdir -p "$(dirname "$STARTUP")"
    touch "$STARTUP"
    strip_startup_hook
    [[ -s "$STARTUP" ]] && echo >> "$STARTUP"

    cat >> "$STARTUP" <<EOF_HOOK
$START_MARK
if [ -x "$SCRIPT" ]; then
    "$SCRIPT" --start &
fi
$END_MARK
EOF_HOOK
    sync
}

remove_startup_hook() {
    [[ -f "$STARTUP" ]] && strip_startup_hook
    sync
}

start_launcher() {
    if is_running; then
        echo "RetroGC Launcher UI is already running (PID $(cat "$PIDFILE"))."
        return 0
    fi
    if [[ ! -x "$BINARY" ]]; then
        echo "ERROR: launcher not found or not executable:"
        echo "  $BINARY"
        return 1
    fi

    # Own session: keeps running when the Scripts window closes
    # Settings (auto start...) are in /media/fat/retrogc/retrogc-mister.ini, changed from the
    # launcher itself (X button)
    setsid "$BINARY" >"$LOGFILE" 2>&1 </dev/null &
    echo $! > "$PIDFILE"

    for _ in {1..20}; do
        sleep 0.1
        if ! is_running; then
            echo "ERROR: RetroGC Launcher UI exited, see $LOGFILE:"
            tail -n 5 "$LOGFILE"
            rm -f "$PIDFILE"
            return 1
        fi
    done
    echo "RetroGC Launcher UI started (PID $(cat "$PIDFILE"))."
}

stop_launcher() {
    if ! is_running; then
        rm -f "$PIDFILE"
        echo "RetroGC Launcher UI is not running."
        return 0
    fi

    local pid
    pid="$(cat "$PIDFILE")"
    kill "$pid"
    for _ in {1..30}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            rm -f "$PIDFILE"
            echo "RetroGC Launcher UI stopped."
            return 0
        fi
        sleep 0.1
    done
    kill -9 "$pid" 2>/dev/null
    rm -f "$PIDFILE"
    echo "RetroGC Launcher UI killed."
}

case "${1:-}" in
    --start)  # from user-startup.sh
        start_launcher >>"$LOGFILE.boot" 2>&1
        ;;
    --stop)
        stop_launcher
        ;;
    --boot)   # from the launcher settings: start with the MiSTer or not
        case "${2:-}" in
            on)  install_startup_hook ;;
            off) remove_startup_hook ;;
            *)   echo "Usage: $0 --boot on|off"; exit 2 ;;
        esac
        ;;
    --show)   # bring the launcher screen back (also: F9 on a keyboard)
        if is_running; then
            kill -USR1 "$(cat "$PIDFILE")"
            echo "The launcher screen comes back when this window closes."
        else
            echo "RetroGC Launcher UI is not running: run retrogc_ui.sh first."
        fi
        ;;
    *)        # Scripts menu: toggle
        echo "RetroGC Launcher UI $VERSION"
        if is_running; then
            remove_startup_hook
            stop_launcher
            echo "Disabled: the stock MiSTer menu is back."
        else
            if pid_alive "$SCRIPT_ONLY_PIDFILE"; then
                echo "The script-only launcher (retrogc.sh) is ON."
                echo "Turn it OFF first: Scripts -> retrogc.sh"
                exit 1
            fi
            install_startup_hook
            if start_launcher; then
                echo "Enabled: the launcher screen shows when this window closes,"
                echo "and after every boot."
                echo "Bring it back from the MiSTer menu with F9 (keyboard)"
                echo "or Scripts -> retrogc_ui_show.sh."
            fi
        fi
        ;;
esac
