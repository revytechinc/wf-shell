#!/bin/sh
# Full package rebuild + dual-mode regression:
#   0) backup user wayland configs
#   1) remove user configs (system defaults only)
#   1b) optional: pkg delete stack (UNINSTALL=1 default)
#   2) build+reinstall stack packages from ports
#   3) pkg check -s + privileges --check
#   4) full-stack regression (must use system wayfire.ini)
#   5) restore user configs
#   6) full-stack regression (must prefer ~/.config/wayfire.ini)
#   7) leave session running for inspection
#
# Env:
#   UNINSTALL=0  — skip pkg delete (default 1 = uninstall first)
#   LEAVE_UP=1   — ensure session up at end (default 1)
#
# Exit 0 only if both regression legs and package integrity pass.
set -eu
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export HOME="${HOME:-/home/mlapointe}"
export USER="${USER:-mlapointe}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/xdg/$USER}"
UNINSTALL="${UNINSTALL:-1}"
LEAVE_UP="${LEAVE_UP:-1}"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PORTS="${PORTS_ROOT:-$HOME/git/cloudbsd-ports}"
BUILD="$PORTS/revytech/wayfire/scripts/build-and-pkg-install.sh"
STAMP=$(date +%Y%m%d%H%M%S)
BACKUP="$HOME/.config/wayland-user-backup-$STAMP"
LOG=/tmp/regression-pkg-system-user-cycle-$STAMP.log
: >"$LOG"

# Reverse-ish order: dependents first
STACK_PKGS="
revytech-ly
revytech-wcm
revytech-wf-osk
revytech-wf-shell
revytech-wayfire-plugins-extra
revytech-wayfire
revytech-wf-config
"

log()  { printf '%s %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$LOG"; }
die()  { log "FATAL: $*"; exit 1; }

log "log=$LOG UNINSTALL=$UNINSTALL LEAVE_UP=$LEAVE_UP"
log "======== 0 BACKUP ========"
mkdir -p "$BACKUP"
for f in wayfire.ini wf-shell.ini; do
  [ -f "$HOME/.config/$f" ] && cp -p "$HOME/.config/$f" "$BACKUP/" && log "backup $f"
done
[ -d "$HOME/.config/wf-shell" ] && cp -a "$HOME/.config/wf-shell" "$BACKUP/wf-shell-dir" && log "backup wf-shell/"
[ -f "$HOME/.config/kanshi/config" ] && mkdir -p "$BACKUP/kanshi" && cp -p "$HOME/.config/kanshi/config" "$BACKUP/kanshi/" && log "backup kanshi"

log "======== 1 REMOVE USER CONFIGS ========"
rm -f "$HOME/.config/wayfire.ini" "$HOME/.config/wf-shell.ini" "$HOME/.config/wf-shell/config.json"
# path helper may be gone after uninstall — only check if present
if [ -x /usr/local/libexec/wayfire-config-path.sh ]; then
  RES=$(/usr/local/libexec/wayfire-config-path.sh)
  log "config-path=$RES"
  case "$RES" in
    /usr/local/etc/wayfire/wayfire.ini|/etc/wayfire/wayfire.ini) ;;
    *) die "expected system config path, got $RES" ;;
  esac
else
  log "wayfire-config-path not installed yet (ok before reinstall)"
fi

if [ "$UNINSTALL" = "1" ]; then
  log "======== 1b UNINSTALL STACK PACKAGES ========"
  # Stop session so packages are not busy
  for n in wf-settings wf-panel wf-background wf-dock wayfire; do
    pkill -x "$n" 2>/dev/null || true
  done
  sleep 0.5
  for n in wayfire wf-panel wf-background; do
    while pgrep -x "$n" >/dev/null 2>&1; do pkill -9 -x "$n" 2>/dev/null || true; sleep 0.1; done
  done
  for p in $STACK_PKGS; do
    if pkg info -e "$p" 2>/dev/null; then
      log "pkg delete -fy $p"
      doas pkg delete -fy "$p" >>"$LOG" 2>&1 || die "pkg delete $p failed"
    else
      log "skip delete (not installed): $p"
    fi
  done
  # confirm gone
  for p in $STACK_PKGS; do
    if pkg info -e "$p" 2>/dev/null; then
      die "package still installed after delete: $p"
    fi
  done
  log "all stack packages removed"
fi

log "======== 2 BUILD+PKG INSTALL ========"
[ -x "$BUILD" ] || die "missing $BUILD"
"$BUILD" \
  devel/revytech-wf-config \
  x11-wm/revytech-wayfire \
  x11-wm/revytech-wayfire-plugins-extra \
  x11/revytech-wf-shell \
  x11/revytech-wcm \
  x11/revytech-ly \
  >>"$LOG" 2>&1 || die "package build/install failed (see $LOG)"

log "======== 3 PKG CHECK ========"
for p in revytech-wf-config revytech-wayfire revytech-wayfire-plugins-extra revytech-wf-shell revytech-wcm revytech-ly; do
  pkg info -e "$p" || die "missing package $p"
  pkg check -s "$p" >>"$LOG" 2>&1 || die "pkg check -s $p failed"
  log "pkg check -s $p OK"
done

if [ -x /usr/local/sbin/revytech-desktop-privileges-enable ]; then
  doas /usr/local/sbin/revytech-desktop-privileges-enable --check "$USER" >>"$LOG" 2>&1 \
    || die "privileges --check failed"
  log "privileges --check OK"
fi

log "======== 4 REGRESSION: SYSTEM DEFAULTS ONLY ========"
rm -f "$HOME/.config/wayfire.ini" "$HOME/.config/wf-shell.ini"
"$ROOT/scripts/full-stack-regression.sh" >>"$LOG" 2>&1 || die "full-stack failed (system defaults)"
LIVE=$(ps -p "$(pgrep -n -x wayfire)" -o args= 2>/dev/null || true)
log "live=$LIVE"
echo "$LIVE" | grep -q '/usr/local/etc/wayfire/wayfire.ini' || die "live wayfire not on system config"
log "PASS system-defaults leg"

log "======== 5 RESTORE USER CONFIGS ========"
cp -p "$BACKUP/wayfire.ini" "$HOME/.config/wayfire.ini"
cp -p "$BACKUP/wf-shell.ini" "$HOME/.config/wf-shell.ini"
[ -f "$BACKUP/kanshi/config" ] && mkdir -p "$HOME/.config/kanshi" && cp -p "$BACKUP/kanshi/config" "$HOME/.config/kanshi/config"
[ -d "$BACKUP/wf-shell-dir" ] && mkdir -p "$HOME/.config/wf-shell" && cp -a "$BACKUP/wf-shell-dir/." "$HOME/.config/wf-shell/"
RES=$(/usr/local/libexec/wayfire-config-path.sh)
[ "$RES" = "$HOME/.config/wayfire.ini" ] || die "user config not preferred after restore (got $RES)"
log "config-path=$RES (user)"

log "======== 6 REGRESSION: USER CONFIG RESTORED ========"
"$ROOT/scripts/full-stack-regression.sh" >>"$LOG" 2>&1 || die "full-stack failed (user config)"
LIVE=$(ps -p "$(pgrep -n -x wayfire)" -o args= 2>/dev/null || true)
log "live=$LIVE"
echo "$LIVE" | grep -q "$HOME/.config/wayfire.ini" || die "live wayfire not on user config"
log "PASS user-config leg"

log "======== 7 LEAVE SESSION FOR INSPECTION ========"
if [ "$LEAVE_UP" = "1" ]; then
  n_wf=$(pgrep -x wayfire 2>/dev/null | wc -l | tr -d ' ')
  n_p=$(pgrep -x wf-panel 2>/dev/null | wc -l | tr -d ' ')
  if [ "$n_wf" -lt 1 ]; then
    log "wayfire missing — starting for inspection"
    CONFIG=$(/usr/local/libexec/wayfire-config-path.sh)
    setsid nohup env -u WAYLAND_DISPLAY -u DISPLAY \
      HOME="$HOME" USER="$USER" XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
      LIBSEAT_BACKEND=seatd WLR_NO_HARDWARE_CURSORS=1 \
      XDG_SESSION_TYPE=wayland WLR_BACKENDS=drm,libinput \
      /usr/local/bin/wayfire -c "$CONFIG" >>/tmp/wayfire-inspect.log 2>&1 </dev/null &
    i=0
    while [ "$i" -lt 15 ]; do
      ls "$XDG_RUNTIME_DIR"/wayland-[0-9] >/dev/null 2>&1 && break
      i=$((i + 1)); sleep 1
    done
  fi
  WD=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-[0-9] 2>/dev/null | head -1)" 2>/dev/null || true)
  if [ -n "${WD:-}" ] && ! pgrep -x wf-panel >/dev/null 2>&1; then
    nohup env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WD" HOME="$HOME" \
      /usr/local/libexec/wf-shell-launch panel >>/tmp/wayfire-inspect.log 2>&1 </dev/null &
  fi
  if [ -n "${WD:-}" ] && ! pgrep -x wf-background >/dev/null 2>&1; then
    nohup env XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" WAYLAND_DISPLAY="$WD" HOME="$HOME" \
      /usr/local/libexec/wf-shell-launch background >>/tmp/wayfire-inspect.log 2>&1 </dev/null &
  fi
  sleep 1
  {
    echo "export XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR"
    echo "export WAYLAND_DISPLAY=${WD:-wayland-1}"
    echo "export LIBSEAT_BACKEND=seatd"
    echo "export XDG_SESSION_TYPE=wayland"
  } >/tmp/wf-shell-session.env
  log "wrote /tmp/wf-shell-session.env"
fi

log "======== SUMMARY ========"
log "BOTH LEGS GREEN"
log "backup=$BACKUP"
log "log=$LOG"
log "wayfire=$(pgrep -x wayfire | wc -l | tr -d ' ') panel=$(pgrep -x wf-panel | wc -l | tr -d ' ') bg=$(pgrep -x wf-background | wc -l | tr -d ' ')"
log "config-path=$(/usr/local/libexec/wayfire-config-path.sh 2>/dev/null || echo '?')"
log "session env: . /tmp/wf-shell-session.env"
exit 0
