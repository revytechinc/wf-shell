#!/bin/sh
# FULL STACK REGRESSION — wayfire → wf-shell → wcm → ly → seatd
#
# A regression test encodes a previously observed failure as a hard assert.
# This orchestrator:
#   0) optional full kill of the session stack
#   1) package integrity for every revytech stack package
#   2) Ly FreeBSD integration / session simulation
#   3) session restart (DRM) + dual-panel / settings safety
#   4) wf-shell unit + present-only integration
#   5) optional reinstall of named packages then re-verify
#
# Usage:
#   full-stack-regression.sh              # verify live install + session
#   full-stack-regression.sh --reinstall  # pkg install -f stack pkgs first
#   full-stack-regression.sh --no-session # skip wayfire kill/restart
#   full-stack-regression.sh --units-only # only unit/sim tests
#
# Exit 0 only if every phase passes.
set -eu
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export HOME="${HOME:-/home/mlapointe}"
export USER="${USER:-mlapointe}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/xdg/$USER}"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LY_ROOT="${LY_ROOT:-$(CDPATH= cd -- "$ROOT/../ly" 2>/dev/null && pwd || true)}"
PORTS_ROOT="${PORTS_ROOT:-$(CDPATH= cd -- "$ROOT/../cloudbsd-ports" 2>/dev/null && pwd || true)}"
LOG=/tmp/full-stack-regression.log
: >"$LOG"

DO_REINSTALL=0
DO_SESSION=1
UNITS_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --reinstall) DO_REINSTALL=1 ;;
    --no-session) DO_SESSION=0 ;;
    --units-only) UNITS_ONLY=1; DO_SESSION=0 ;;
    -h|--help)
      sed -n '2,20p' "$0"
      exit 0
      ;;
  esac
done

FAIL=0
PASS=0
log()  { printf '%s\n' "$*" | tee -a "$LOG"; }
pass() { printf 'PASS %s\n' "$*" | tee -a "$LOG"; PASS=$((PASS + 1)); }
fail() { printf 'FAIL %s\n' "$*" | tee -a "$LOG" >&2; FAIL=$((FAIL + 1)); }
section() { printf '\n======== %s ========\n' "$*" | tee -a "$LOG"; }

# Stack packages (order: deps first for reinstall)
STACK_PKGS="
revytech-wf-config
revytech-wayfire
revytech-wayfire-plugins-extra
revytech-wf-shell
revytech-wcm
revytech-wf-osk
revytech-ly
"

# ── 0 packages present ─────────────────────────────────────────────────────
section "0 STACK PACKAGES INSTALLED"
for pkg in $STACK_PKGS; do
  if pkg info -e "$pkg" 2>/dev/null; then
    pass "pkg installed $pkg=$(pkg query '%v' "$pkg")"
  else
    fail "pkg missing $pkg"
  fi
done

# ── 0b package integrity ───────────────────────────────────────────────────
section "0b PKG CHECK -s"
for pkg in $STACK_PKGS; do
  pkg info -e "$pkg" 2>/dev/null || continue
  if pkg check -s "$pkg" >>"$LOG" 2>&1; then
    pass "pkg check -s $pkg clean"
  else
    fail "pkg check -s $pkg mismatched"
  fi
done

# ── 0c critical binaries ───────────────────────────────────────────────────
section "0c CRITICAL BINARIES"
BINS="
/usr/local/bin/wayfire
/usr/local/bin/wf-panel
/usr/local/bin/wf-settings
/usr/local/bin/wf-background
/usr/local/bin/wcm
/usr/local/bin/ly
/usr/local/bin/ly_wrapper
/usr/local/libexec/wayfire-session-launch
/usr/local/libexec/wf-shell-launch
/usr/local/sbin/revytech-ly-enable
"
for b in $BINS; do
  if [ -x "$b" ]; then
    pass "executable $b"
  else
    fail "missing/non-exec $b"
  fi
done

# ── 1 optional reinstall ───────────────────────────────────────────────────
if [ "$DO_REINSTALL" -eq 1 ]; then
  section "1 REINSTALL STACK (pkg install -f)"
  for pkg in $STACK_PKGS; do
    if pkg info -e "$pkg" 2>/dev/null; then
      if doas pkg install -fy "$pkg" >>"$LOG" 2>&1; then
        pass "reinstalled $pkg"
      else
        fail "reinstall failed $pkg"
      fi
    fi
  done
fi

# ── 2 Ly integration (always) ──────────────────────────────────────────────
section "2 LY INTEGRATION"
LY_SCRIPT=""
if [ -n "$LY_ROOT" ] && [ -x "$LY_ROOT/scripts/integration-freebsd-ly.sh" ]; then
  LY_SCRIPT="$LY_ROOT/scripts/integration-freebsd-ly.sh"
elif [ -x /usr/local/share/revytech-ly/integration-freebsd-ly.sh ]; then
  LY_SCRIPT=/usr/local/share/revytech-ly/integration-freebsd-ly.sh
fi
if [ -n "$LY_SCRIPT" ]; then
  if "$LY_SCRIPT" >>"$LOG" 2>&1; then
    pass "Ly integration suite green"
  else
    fail "Ly integration suite red"
    tail -40 "$LOG" || true
  fi
else
  # Inline minimal Ly checks if script not present
  log "inline Ly checks (no scripts/integration-freebsd-ly.sh)"
  grep -q '^Ly:' /etc/gettytab && pass "gettytab Ly" || fail "gettytab Ly"
  grep -E '^ttyv1.*getty Ly' /etc/ttys >/dev/null && pass "ttys Ly" || fail "ttys Ly"
  [ -x /usr/local/bin/ly_wrapper ] && pass "ly_wrapper" || fail "ly_wrapper"
fi

# ── 3 session simulation (no DM login) ─────────────────────────────────────
section "3 SESSION LAUNCH SIMULATION"
SIM_DIR=/tmp/full-stack-sim.$$
mkdir -p "$SIM_DIR"
# Simulate what Ly runs after auth: setup.sh + wayfire-session-launch env preflight
# We only validate env construction, not actually starting a second compositor
if [ -x /usr/local/libexec/wayfire-session-launch ]; then
  LAUNCH=/usr/local/libexec/wayfire-session-launch
  # Static contract checks only — never exec the real compositor here
  # (hardcoded PREFIX/bin/wayfire would hang the suite / dual-start DRM).
  if grep -q 'unset WAYLAND_DISPLAY' "$LAUNCH" \
     || grep -q 'clearing WAYLAND_DISPLAY' "$LAUNCH"; then
    pass "sim: anti-nested-wayland present in session-launch"
  else
    fail "sim: session-launch missing nested Wayland guard"
  fi
  if grep -q 'LIBSEAT_BACKEND' "$LAUNCH"; then
    pass "sim: LIBSEAT_BACKEND present in session-launch"
  else
    fail "sim: LIBSEAT_BACKEND missing — Ly sessions will lack seatd"
  fi
  if grep -q 'XDG_RUNTIME_DIR' "$LAUNCH" && grep -q 'FATAL' "$LAUNCH"; then
    pass "sim: session-launch fails closed on missing runtime dir"
  else
    fail "sim: session-launch missing XDG_RUNTIME_DIR fail-closed"
  fi
  # Live fail-closed probe (must exit nonzero, must not start wayfire)
  SIM_OUT="$SIM_DIR/sim.out"
  if env -u XDG_RUNTIME_DIR HOME=/tmp USER=nobody \
      timeout 3 "$LAUNCH" >"$SIM_OUT" 2>&1; then
    fail "sim: launch succeeded without XDG_RUNTIME_DIR"
  else
    if grep -qi 'XDG_RUNTIME_DIR\|FATAL\|missing' "$SIM_OUT" 2>/dev/null; then
      pass "sim: launch refuses missing XDG_RUNTIME_DIR"
    else
      fail "sim: launch failed for unexpected reason: $(head -3 "$SIM_OUT")"
    fi
  fi
else
  fail "sim: wayfire-session-launch missing"
fi
rm -rf "$SIM_DIR"

# ── 4 wcm present ──────────────────────────────────────────────────────────
section "4 WCM"
if [ -x /usr/local/bin/wcm ]; then
  pass "wcm binary present"
  owner=$(pkg which -q /usr/local/bin/wcm 2>/dev/null || echo none)
  case "$owner" in revytech-wcm-*) pass "wcm package-owned $owner" ;; *) fail "wcm owner $owner" ;; esac
else
  fail "wcm missing"
fi

# ── 5 session kill/restart + wf-shell regression ───────────────────────────
if [ "$DO_SESSION" -eq 1 ] && [ "$UNITS_ONLY" -eq 0 ]; then
  section "5 WF-SHELL REGRESSION (stop/verify/restart/R1-R8)"
  if [ -x "$ROOT/scripts/regression-full-reinstall.sh" ]; then
    if "$ROOT/scripts/regression-full-reinstall.sh" >>"$LOG" 2>&1; then
      pass "wf-shell regression-full-reinstall green"
    else
      fail "wf-shell regression-full-reinstall red"
      tail -50 "$LOG" || true
    fi
  else
    fail "missing $ROOT/scripts/regression-full-reinstall.sh"
  fi
else
  section "5 SKIP session (flags)"
  log "session phase skipped"
fi

# ── 6 unit suites ──────────────────────────────────────────────────────────
section "6 UNIT SUITES"
if [ -d "$ROOT/build" ] && command -v meson >/dev/null 2>&1; then
  if meson test -C "$ROOT/build" --print-errorlogs >>"$LOG" 2>&1; then
    pass "wf-shell meson tests green"
  else
    fail "wf-shell meson tests red"
  fi
else
  log "skip wf-shell meson (no build/)"
fi

if [ -n "$LY_ROOT" ] && [ -f "$LY_ROOT/build.zig" ] && command -v zig >/dev/null 2>&1; then
  if (cd "$LY_ROOT" && zig build test) >>"$LOG" 2>&1; then
    pass "ly zig tests green"
  else
    fail "ly zig tests red"
  fi
else
  log "skip ly zig tests"
fi

# ── 7 present-only integration ─────────────────────────────────────────────
section "7 PRESENT-ONLY-AVAILABLE"
if [ -x "$ROOT/scripts/integration-present-only-available.sh" ]; then
  if "$ROOT/scripts/integration-present-only-available.sh" >>"$LOG" 2>&1; then
    pass "present-only-available green"
  else
    fail "present-only-available red"
  fi
fi

# ── 8 leave-up (must not report green with a dead desktop) ─────────────────
section "8 LEAVE-UP"
n_wf=$(pgrep -x wayfire 2>/dev/null | wc -l | tr -d ' ')
n_p=$(pgrep -x wf-panel 2>/dev/null | wc -l | tr -d ' ')
if [ "$DO_SESSION" -eq 1 ]; then
  if [ "$n_wf" -eq 1 ] && [ "$n_p" -eq 1 ]; then
    pass "leave-up: wayfire=1 panel=1"
  else
    fail "leave-up: wayfire=$n_wf panel=$n_p (suite must leave desktop up)"
  fi
  if [ -f /tmp/wf-shell-session.env ]; then
    pass "leave-up: session env file present"
  else
    log "NOTE: no /tmp/wf-shell-session.env (regression may have been skipped)"
  fi
else
  log "leave-up skipped (--no-session/--units-only)"
fi

# ── SUMMARY ────────────────────────────────────────────────────────────────
section "SUMMARY"
log "PASS=$PASS FAIL=$FAIL"
log "full log: $LOG"
log "ports root: ${PORTS_ROOT:-none} ly root: ${LY_ROOT:-none}"
log "live: wayfire=$n_wf panel=$n_p"
if [ "$FAIL" -ne 0 ]; then
  log "FULL STACK REGRESSION FAILED"
  exit 1
fi
log "FULL STACK REGRESSION PASSED"
exit 0
