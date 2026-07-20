#!/bin/sh
# Full package rebuild + dual-mode regression:
#   1) backup user wayland configs
#   2) remove user configs (system defaults only)
#   3) build+reinstall stack packages from ports
#   4) pkg check -s + privileges --check
#   5) full-stack regression (must use system wayfire.ini)
#   6) restore user configs
#   7) full-stack regression (must prefer ~/.config/wayfire.ini)
#
# Exit 0 only if both regression legs and package integrity pass.
set -eu
export PATH=/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin
export HOME="${HOME:-/home/mlapointe}"
export USER="${USER:-mlapointe}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run/xdg/$USER}"

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PORTS="${PORTS_ROOT:-$HOME/git/cloudbsd-ports}"
BUILD="$PORTS/revytech/wayfire/scripts/build-and-pkg-install.sh"
STAMP=$(date +%Y%m%d%H%M%S)
BACKUP="$HOME/.config/wayland-user-backup-$STAMP"
LOG=/tmp/regression-pkg-system-user-cycle-$STAMP.log
: >"$LOG"

log()  { printf '%s %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$LOG"; }
die()  { log "FATAL: $*"; exit 1; }

log "log=$LOG"
log "======== 0 BACKUP ========"
mkdir -p "$BACKUP"
for f in wayfire.ini wf-shell.ini; do
  [ -f "$HOME/.config/$f" ] && cp -p "$HOME/.config/$f" "$BACKUP/" && log "backup $f"
done
[ -d "$HOME/.config/wf-shell" ] && cp -a "$HOME/.config/wf-shell" "$BACKUP/wf-shell-dir" && log "backup wf-shell/"
[ -f "$HOME/.config/kanshi/config" ] && mkdir -p "$BACKUP/kanshi" && cp -p "$HOME/.config/kanshi/config" "$BACKUP/kanshi/" && log "backup kanshi"

log "======== 1 REMOVE USER CONFIGS ========"
rm -f "$HOME/.config/wayfire.ini" "$HOME/.config/wf-shell.ini" "$HOME/.config/wf-shell/config.json"
RES=$(/usr/local/libexec/wayfire-config-path.sh)
log "config-path=$RES"
case "$RES" in
  /usr/local/etc/wayfire/wayfire.ini|/etc/wayfire/wayfire.ini) ;;
  *) die "expected system config path, got $RES" ;;
esac

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

log "======== SUMMARY ========"
log "BOTH LEGS GREEN"
log "backup=$BACKUP"
log "log=$LOG"
log "wayfire=$(pgrep -x wayfire | wc -l | tr -d ' ') panel=$(pgrep -x wf-panel | wc -l | tr -d ' ')"
exit 0
