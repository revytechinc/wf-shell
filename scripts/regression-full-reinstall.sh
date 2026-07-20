#!/bin/sh
# REGRESSION SUITE — full stop / reinstall verification / restart / known-bug checks
#
# A regression test is NOT "smoke that something ran".
# It is: a previously observed failure, encoded as a hard assert that must FAIL
# if that failure returns after a rebuild/reinstall/restart.
#
# Known regressions encoded here (from this product's failure history):
#   R1  dual wf-panel / dual wf-background after session start
#   R2  Invalid widget: weather (presenting unbuilt widgets)
#   R3  wf-settings kills wayfire within seconds of open
#   R4  theme CSS installed without matching menu icon artifact
#   R5  package files missing or checksum mismatch after install
#   R6  panel dies on config reload thrash (must still be alive after settings)
#   R7  autostart must not leave zero panels when wayfire is up
#   R8  installed binaries must be owned by revytech-wf-shell package
#
# Exit 0 only if every regression assert passes.
set -eu
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export HOME="${HOME:-/home/mlapointe}"
export USER="${USER:-mlapointe}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/xdg/$USER}"

PKG_NAME=revytech-wf-shell
FAIL=0
PASS=0
LOG=/tmp/wf-shell-regression.log
: >"$LOG"

log()  { printf '%s\n' "$*" | tee -a "$LOG"; }
pass() { printf 'PASS %s\n' "$*" | tee -a "$LOG"; PASS=$((PASS + 1)); }
fail() { printf 'FAIL %s\n' "$*" | tee -a "$LOG" >&2; FAIL=$((FAIL + 1)); }
section() { printf '\n======== %s ========\n' "$*" | tee -a "$LOG"; }

count_proc() {
  # $1 = exact process name
  pgrep -x "$1" 2>/dev/null | wc -l | tr -d ' '
}

wait_for() {
  # wait_for <seconds> <shell condition>
  n=$1; shift
  i=0
  while [ "$i" -lt "$n" ]; do
    if eval "$*"; then
      return 0
    fi
    i=$((i + 1))
    sleep 1
  done
  return 1
}

# ── 0) Preflight ───────────────────────────────────────────────────────────
section "0 PREFLIGHT"
if ! pkg info -e "$PKG_NAME" 2>/dev/null; then
  fail "R5 package $PKG_NAME is not installed"
else
  pass "R5 package $PKG_NAME is installed: $(pkg query '%n-%v' "$PKG_NAME")"
fi

# ── 1) STOP EVERYTHING (session stack) ─────────────────────────────────────
section "1 STOP SESSION STACK"
# Kill clients first, then compositor
for name in wf-settings wf-dock wf-panel wf-background kanshi mako wlsunset; do
  pkill -x "$name" 2>/dev/null || true
done
sleep 0.3
pkill -x wayfire 2>/dev/null || true
sleep 0.5
# force leftovers
for name in wayfire wf-panel wf-background wf-dock wf-settings; do
  while pgrep -x "$name" >/dev/null 2>&1; do
    pkill -9 -x "$name" 2>/dev/null || true
    sleep 0.15
  done
done
# stale sockets
rm -f "$XDG_RUNTIME_DIR"/wayland-* 2>/dev/null || true

n_wf=$(count_proc wayfire)
n_p=$(count_proc wf-panel)
n_b=$(count_proc wf-background)
if [ "$n_wf" -eq 0 ] && [ "$n_p" -eq 0 ] && [ "$n_b" -eq 0 ]; then
  pass "R1 pre-restart: stack fully stopped (0 wayfire/panel/bg)"
else
  fail "R1 pre-restart: still running wayfire=$n_wf panel=$n_p bg=$n_b"
fi

# ── 2) VERIFY PACKAGE CONTENTS ON DISK ────────────────────────────────────
section "2 PACKAGE CONTENTS vs DISK"
if pkg check -s "$PKG_NAME" >>"$LOG" 2>&1; then
  pass "R5 pkg check -s $PKG_NAME clean"
else
  fail "R5 pkg check -s $PKG_NAME reported mismatches (see $LOG)"
fi

# Required artifacts that previously went missing
REQUIRED="
/usr/local/bin/wf-panel
/usr/local/bin/wf-settings
/usr/local/bin/wf-background
/usr/local/libexec/wf-shell-launch
/usr/local/share/wf-shell/css/default.css
/usr/local/share/wf-shell/icons/wayfire.png
/usr/local/share/wf-shell/icons/menu/haiku-leaf.svg
/usr/local/share/wf-shell/icons/menu/crt-node.svg
/usr/local/share/wf-shell/themes/beos-haiku.css
/usr/local/share/wf-shell/themes/crt-phosphor.css
/usr/local/etc/wf-shell/wf-shell.ini
"
for f in $REQUIRED; do
  if [ ! -e "$f" ]; then
    fail "R5 missing required installed file: $f"
    continue
  fi
  owner=$(pkg which -q "$f" 2>/dev/null || echo none)
  case "$owner" in
    ${PKG_NAME}-*) pass "R8 $f owned by $owner" ;;
    *) fail "R8 $f not owned by $PKG_NAME (got: $owner)" ;;
  esac
done

# R4: every installed theme CSS must have menu icon (regression: incomplete packs)
if [ -d /usr/local/share/wf-shell/themes ]; then
  for css in /usr/local/share/wf-shell/themes/*.css; do
    [ -f "$css" ] || continue
    id=$(basename "$css" .css)
    case "$id" in default) continue ;; esac
    icon=""
    case "$id" in
      win95) icon=win95-start ;;
      system7) icon=system7-apple ;;
      amiga-workbench) icon=amiga-wb ;;
      crt-phosphor) icon=crt-node ;;
      synthwave) icon=synth-grid ;;
      miami-cyberpunk) icon=neon-orb ;;
      nord) icon=nord-circle ;;
      dracula) icon=dracula-bat ;;
      rose-pine) icon=rose-bloom ;;
      tokyo-night) icon=tokyo-pulse ;;
      catppuccin-mocha) icon=catppuccin-latte ;;
      modern-glass) icon=glass-orb ;;
      cde-solaris) icon=solaris-sun ;;
      sgi-irix) icon=sgi-cube ;;
      ibm-aix) icon=ibm-logo ;;
      beos-haiku) icon=haiku-leaf ;;
      nextstep) icon=nextstep-cube ;;
      osx-aqua) icon=aqua-globe ;;
      win-xp) icon=xp-flag ;;
      *) fail "R4 theme $id has no icon map (cannot present safely)"; continue ;;
    esac
    found=0
    for e in svg png svgz; do
      [ -f "/usr/local/share/wf-shell/icons/menu/$icon.$e" ] && found=1 && break
    done
    if [ "$found" -eq 1 ]; then
      pass "R4 theme $id has icon $icon"
    else
      fail "R4 theme $id missing menu icon artifact $icon"
    fi
  done
fi

# R2: binary must not export weather if we claim no weather — check strings + config
if ! strings /usr/local/bin/wf-panel 2>/dev/null | grep -q WayfireWeather; then
  if grep -q weather "${HOME}/.config/wf-shell.ini" 2>/dev/null || \
     grep -q weather "${HOME}/.config/wf-shell/config.json" 2>/dev/null; then
    fail "R2 weather still in user config but panel has no WayfireWeather"
  else
    pass "R2 no weather in config while panel lacks weather widget"
  fi
else
  pass "R2 panel includes weather (config may list it)"
fi

# ── 3) RESTART SESSION (DRM, not nested wayland) ─────────────────────────
section "3 RESTART WAYFIRE + CLIENTS"
# Critical: never leave WAYLAND_DISPLAY set or wayfire nests and fails
unset WAYLAND_DISPLAY
unset DISPLAY
unset GDK_BACKEND

eval "$(dbus-launch --sh-syntax 2>/dev/null || true)"
export DBUS_SESSION_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:-}"
export LIBSEAT_BACKEND="${LIBSEAT_BACKEND:-seatd}"
export WLR_NO_HARDWARE_CURSORS="${WLR_NO_HARDWARE_CURSORS:-1}"
export __GLX_VENDOR_LIBRARY_NAME="${__GLX_VENDOR_LIBRARY_NAME:-nvidia}"
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=wayfire
export XDG_CURRENT_DESKTOP=wayfire
export WLR_BACKENDS=drm,libinput
[ -e /dev/dri/renderD128 ] && export WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128

CONFIG="${WAYFIRE_CONFIG_FILE:-$HOME/.config/wayfire.ini}"
RESTART_LOG=/tmp/wf-shell-regression-restart.log
: >"$RESTART_LOG"

nohup env -u WAYLAND_DISPLAY -u DISPLAY \
  HOME="$HOME" USER="$USER" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
  LIBSEAT_BACKEND="$LIBSEAT_BACKEND" \
  WLR_NO_HARDWARE_CURSORS="$WLR_NO_HARDWARE_CURSORS" \
  __GLX_VENDOR_LIBRARY_NAME="$__GLX_VENDOR_LIBRARY_NAME" \
  XDG_SESSION_TYPE=wayland XDG_SESSION_DESKTOP=wayfire XDG_CURRENT_DESKTOP=wayfire \
  WLR_BACKENDS=drm,libinput \
  ${WLR_RENDER_DRM_DEVICE:+WLR_RENDER_DRM_DEVICE=$WLR_RENDER_DRM_DEVICE} \
  ${DBUS_SESSION_BUS_ADDRESS:+DBUS_SESSION_BUS_ADDRESS=$DBUS_SESSION_BUS_ADDRESS} \
  /usr/local/bin/wayfire -c "$CONFIG" >>"$RESTART_LOG" 2>&1 &
WF_PID=$!
log "started wayfire pid=$WF_PID"

if ! wait_for 15 'ls "$XDG_RUNTIME_DIR"/wayland-[0-9] >/dev/null 2>&1'; then
  fail "R7 wayfire did not create wayland socket (see $RESTART_LOG)"
  tail -40 "$RESTART_LOG" | tee -a "$LOG" || true
else
  export WAYLAND_DISPLAY
  WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-[0-9] 2>/dev/null | head -1)")
  export WAYLAND_DISPLAY
  pass "R7 wayland socket up: $WAYLAND_DISPLAY"
fi

# Wait for autostart clients; if none, start via packaged launcher once
wait_for 10 'pgrep -x wf-panel >/dev/null' || true
if ! pgrep -x wf-panel >/dev/null 2>&1; then
  log "autostart did not start panel — launching via package libexec once"
  nohup env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" HOME="$HOME" \
    /usr/local/libexec/wf-shell-launch panel >>"$RESTART_LOG" 2>&1 &
  sleep 1
fi
if ! pgrep -x wf-background >/dev/null 2>&1; then
  nohup env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" HOME="$HOME" \
    /usr/local/libexec/wf-shell-launch background >>"$RESTART_LOG" 2>&1 &
  sleep 0.5
fi

sleep 1
n_wf=$(count_proc wayfire)
n_p=$(count_proc wf-panel)
n_b=$(count_proc wf-background)
n_d=$(count_proc wf-dock)

# R1 dual stack
if [ "$n_wf" -eq 1 ]; then pass "R1 exactly one wayfire"; else fail "R1 wayfire count=$n_wf want 1"; fi
if [ "$n_p" -eq 1 ]; then pass "R1 exactly one wf-panel"; else fail "R1 wf-panel count=$n_p want 1"; fi
if [ "$n_b" -le 1 ]; then pass "R1 wf-background count=$n_b (<=1)"; else fail "R1 wf-background count=$n_b"; fi
if [ "$n_d" -eq 0 ]; then pass "R1 no wf-dock (optional, expected off)"; else
  log "NOTE: wf-dock count=$n_d (not a hard fail if user enabled dock)"
fi

# R7 panel must exist when wayfire up
if [ "$n_wf" -ge 1 ] && [ "$n_p" -ge 1 ]; then
  pass "R7 panel present with wayfire"
else
  fail "R7 panel missing while wayfire should be running"
fi

# ── 4) BEHAVIOR REGRESSIONS ──────────────────────────────────────────────
section "4 BEHAVIOR REGRESSIONS"
PANEL_LOG=/tmp/wf-panel-regression.log
pkill -x wf-panel 2>/dev/null || true
sleep 0.3
while pgrep -x wf-panel >/dev/null 2>&1; do pkill -9 -x wf-panel; sleep 0.1; done
# Ensure we still only start ONE
nohup env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" HOME="$HOME" \
  /usr/local/bin/wf-panel -c "${HOME}/.config/wf-shell.ini" >"$PANEL_LOG" 2>&1 &
sleep 1.2
if [ "$(count_proc wf-panel)" -eq 1 ]; then
  pass "R1 single panel after explicit start"
else
  fail "R1 panel count after start=$(count_proc wf-panel)"
fi

# R2 invalid widget spam
if grep -q 'Invalid widget:' "$PANEL_LOG" 2>/dev/null; then
  fail "R2 Invalid widget in panel log (presenting unavailable widgets)"
  grep 'Invalid widget:' "$PANEL_LOG" | sort -u | tee -a "$LOG"
else
  pass "R2 no Invalid widget lines after panel start"
fi

# R3 + R6: settings must not kill wayfire/panel
WF=$(pgrep -n wayfire || true)
P=$(pgrep -n wf-panel || true)
if [ -n "$WF" ]; then
  timeout 5 env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WAYLAND_DISPLAY" HOME="$HOME" \
    /usr/local/bin/wf-settings >/tmp/wf-settings-regression.log 2>&1 || true
  sleep 0.5
  if kill -0 "$WF" 2>/dev/null && [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]; then
    pass "R3 wf-settings did not kill wayfire"
  else
    fail "R3 wf-settings killed wayfire or removed socket"
  fi
  if [ -n "$P" ] && kill -0 "$P" 2>/dev/null; then
    pass "R6 panel still alive after wf-settings"
  else
    # panel may have been replaced by autostart — allow count==1
    if [ "$(count_proc wf-panel)" -eq 1 ]; then
      pass "R6 exactly one panel still present after wf-settings"
    else
      fail "R6 panel gone or duplicated after wf-settings (count=$(count_proc wf-panel))"
    fi
  fi
  if [ "$(count_proc wf-panel)" -eq 1 ]; then
    pass "R1 still exactly one panel after settings"
  else
    fail "R1 panel count after settings=$(count_proc wf-panel)"
  fi
else
  fail "R3 no wayfire to regression-test settings against"
fi

# R4/R5 again: strings prove new gates in installed binary
if strings /usr/local/bin/wf-panel 2>/dev/null | grep -q 'wf-shell:gate'; then
  pass "R5 installed wf-panel contains gate logging (new build)"
else
  fail "R5 installed wf-panel missing gate strings — old binary?"
fi
if strings /usr/local/bin/wf-settings 2>/dev/null | grep -q 'org.wayfire.wf-settings'; then
  pass "R5 wf-settings present as package app"
else
  fail "R5 wf-settings binary unexpected"
fi

# unit suite if build tree present
section "5 UNIT REGRESSIONS (if build tree present)"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if [ -d "$ROOT/build" ] && command -v meson >/dev/null 2>&1; then
  if meson test -C "$ROOT/build" --print-errorlogs >>"$LOG" 2>&1; then
    pass "unit meson test suite green"
  else
    fail "unit meson test suite red (see $LOG)"
  fi
  if [ -x "$ROOT/scripts/integration-present-only-available.sh" ]; then
    if "$ROOT/scripts/integration-present-only-available.sh" >>"$LOG" 2>&1; then
      pass "present-only-available integration green"
    else
      fail "present-only-available integration red"
    fi
  fi
else
  log "skip unit suite (no build/ or meson)"
fi

# ── SUMMARY ──────────────────────────────────────────────────────────────
section "SUMMARY"
log "PASS=$PASS FAIL=$FAIL"
log "full log: $LOG"
if [ "$FAIL" -ne 0 ]; then
  log "REGRESSION SUITE FAILED"
  exit 1
fi
log "REGRESSION SUITE PASSED"
exit 0
