#!/bin/sh
# Live-session integration checks (must run where wayfire/panel exist).
# Fails if: multi panel, invalid widgets in log, weather when unbuilt, broken config keys.
set -eu
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/xdg/${USER}}"
FAIL=0
fail() { printf 'FAIL: %s\n' "$*" >&2; FAIL=$((FAIL+1)); }
ok() { printf 'ok: %s\n' "$*"; }

# Wayland
WL=""
for s in "$XDG_RUNTIME_DIR"/wayland-*; do
  case "$s" in *.lock) continue ;; esac
  [ -S "$s" ] || continue
  WL=$(basename "$s")
  break
done
if [ -z "$WL" ]; then
  fail "no wayland socket in $XDG_RUNTIME_DIR"
else
  ok "wayland $WL"
fi

# Single compositor stack
n_wf=$(pgrep -x wayfire 2>/dev/null | wc -l | tr -d ' ')
n_panel=$(pgrep -x wf-panel 2>/dev/null | wc -l | tr -d ' ')
n_bg=$(pgrep -x wf-background 2>/dev/null | wc -l | tr -d ' ')
[ "$n_wf" -eq 1 ] && ok "one wayfire" || fail "wayfire count=$n_wf (want 1)"
[ "$n_panel" -eq 1 ] && ok "one wf-panel" || fail "wf-panel count=$n_panel (want 1)"
[ "$n_bg" -le 1 ] && ok "wf-background count=$n_bg" || fail "wf-background count=$n_bg"

# Config: no duplicate keys in [panel]
ini="${HOME}/.config/wf-shell.ini"
if [ -f "$ini" ]; then
  dups=$(awk '
    /^\[panel\]/{p=1;next}
    /^\[/{p=0}
    p && /^[a-zA-Z0-9_]+[ \t]*=/{
      k=$1; sub(/=.*/,"",k); c[k]++
    }
    END{for(k in c) if(c[k]>1) print k, c[k]}
  ' "$ini" || true)
  if [ -n "$dups" ]; then
    fail "duplicate [panel] keys: $dups"
  else
    ok "no duplicate [panel] keys"
  fi
fi

# Panel log: no Invalid widget (if log present)
LOG="${WF_PANEL_LOG:-/tmp/wf-panel.log}"
if [ -f "$LOG" ]; then
  # only check last 200 lines (current session)
  if tail -200 "$LOG" | grep -q 'Invalid widget:'; then
    fail "Invalid widget in $LOG (presenting unavailable widgets)"
    tail -200 "$LOG" | grep 'Invalid widget:' | sort -u
  else
    ok "no Invalid widget in recent panel log"
  fi
fi

# present-only script
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -x "$SCRIPT_DIR/integration-present-only-available.sh" ]; then
  "$SCRIPT_DIR/integration-present-only-available.sh" || fail "present-only-available checks"
else
  fail "missing integration-present-only-available.sh"
fi

# wf-settings must not kill compositor in 4s
if [ -n "$WL" ] && [ -x /usr/local/bin/wf-settings ]; then
  WF=$(pgrep -n wayfire || true)
  if [ -n "$WF" ]; then
    timeout 4 env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WL" HOME="$HOME" \
      /usr/local/bin/wf-settings >/tmp/wf-settings-integ.log 2>&1 || true
    sleep 0.3
    if kill -0 "$WF" 2>/dev/null && [ -S "$XDG_RUNTIME_DIR/$WL" ]; then
      ok "wf-settings open did not kill wayfire"
    else
      fail "wf-settings killed wayfire or removed socket"
    fi
    n_panel2=$(pgrep -x wf-panel 2>/dev/null | wc -l | tr -d ' ')
    [ "$n_panel2" -eq 1 ] && ok "still one panel after settings" || fail "panel count after settings=$n_panel2"
  fi
fi

if [ "$FAIL" -ne 0 ]; then
  printf '%s failure(s)\n' "$FAIL" >&2
  exit 1
fi
printf 'live-session integration OK\n'
exit 0
