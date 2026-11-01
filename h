#!/bin/bash
# Hyprland Floating Window Switcher (Self-Executing | Bulletproof)

# --- 1. INTERNAL DATA MODE (The Fix) ---
# If the script is called with --get, it just prints windows and exits.
# This bypasses all environment/function scoping issues.
if [ "$1" == "--get" ]; then
    hyprctl clients -j | jq -r '
        sort_by(.workspace.id) | .[] | 
        select(.workspace.id > 0 and .title != "hnav_float" and .title != "") | 
        "[\(.workspace.name)] \((.title | gsub(" ~"; ""))) \t \(.address)"'
    exit 0
fi

# --- 2. TOGGLE LOGIC ---
LOCK_FILE="/tmp/hnav_switcher.lock"
if [ -f "$LOCK_FILE" ]; then
    OLD_PID=$(cat "$LOCK_FILE")
    if ps -p "$OLD_PID" > /dev/null; then
        kill "$OLD_PID"
        exit 0
    else
        rm "$LOCK_FILE"
    fi
fi
echo $$ > "$LOCK_FILE"

# --- 3. CONFIGURATION ---
export FZF_PORT="6266"
export HYPR_SOCK="$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE/.socket2.sock"
SELF_PATH=$(realpath "$0") # The script knows where it lives

# --- 4. REAL-TIME LISTENER ---
# It tells fzf to run "this_script --get" whenever a window changes.
# This is physically robust because it executes the file on disk.
(
    socat -u UNIX-CONNECT:"$HYPR_SOCK" - | stdbuf -o0 grep -E "openwindow|closewindow|movewindow|destroywindow|windowtitle" | while read -r event; do
        # We use a simple HTTP POST which is universally supported by fzf
        # We explicitly call the script path with --get
        CMD="reload($SELF_PATH --get)"
        LEN=${#CMD}
        
        # Send to the port
        if exec 3<>/dev/tcp/localhost/$FZF_PORT 2>/dev/null; then
             printf "POST / HTTP/1.1\r\nContent-Length: $LEN\r\n\r\n$CMD" >&3
             exec 3<&-
        fi
    done
) &
LISTENER_PID=$!

cleanup() {
    kill "$LISTENER_PID" 2>/dev/null
    rm -f "$LOCK_FILE"
}
trap cleanup EXIT

# --- 5. EXECUTION ---
# Initial run also calls self with --get
"$SELF_PATH" --get | fzf \
    --listen "$FZF_PORT" \
    --height=100% \
    --layout=reverse \
    --border=none \
    --header="[ENTER] Jump | [ESC] Close" \
    --prompt="󱂬 WINDOWS: " \
    --delimiter='\t' \
    --with-nth=1 \
    | awk -F '\t' '{print $NF}' | xargs -r -I % hyprctl dispatch focuswindow address:%
