#!/bin/sh
# Integration: never present what is not there.
# Exit non-zero if installed product or user config violates the rule.
set -eu
PREFIX="${PREFIX:-/usr/local}"
SHARE="$PREFIX/share/wf-shell"
FAIL=0

log() { printf '%s\n' "$*"; }
fail() { printf 'FAIL: %s\n' "$*" >&2; FAIL=$((FAIL + 1)); }
ok() { printf 'ok: %s\n' "$*"; }

# ── Themes: every installed CSS must have menu icon ─────────────────────
if [ -d "$SHARE/themes" ]; then
  for css in "$SHARE/themes"/*.css; do
    [ -f "$css" ] || continue
    id=$(basename "$css" .css)
    case "$id" in default) continue ;; esac
    # map via known icons (same as theme-defaults)
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
      *) icon="" ;;
    esac
    if [ -z "$icon" ]; then
      fail "theme $id has no menu-icon map"
      continue
    fi
    found=0
    for e in svg png svgz; do
      if [ -f "$SHARE/icons/menu/$icon.$e" ]; then
        found=1
        break
      fi
    done
    if [ "$found" -eq 0 ]; then
      fail "theme $id missing menu icon $icon under $SHARE/icons/menu"
    else
      ok "theme $id + icon $icon"
    fi
  done
else
  fail "missing $SHARE/themes"
fi

# ── User config: no weather if panel has no weather ─────────────────────
PANEL_BIN="${PANEL_BIN:-$PREFIX/bin/wf-panel}"
if [ -x "$PANEL_BIN" ] && ! strings "$PANEL_BIN" 2>/dev/null | grep -q WayfireWeather; then
  CFG="${HOME}/.config/wf-shell/config.json"
  INI="${HOME}/.config/wf-shell.ini"
  for f in "$CFG" "$INI"; do
    if [ -f "$f" ] && grep -q weather "$f" 2>/dev/null; then
      fail "config $f still references weather but panel has no weather widget"
    fi
  done
  ok "weather not in config (or panel has weather)"
fi

# ── Panel binary feature vs volume ──────────────────────────────────────
if [ -x "$PANEL_BIN" ]; then
  if ldd "$PANEL_BIN" 2>/dev/null | grep -q libpulse; then
    ok "panel linked pulse (volume possible)"
  else
    ok "panel without pulse (volume unavailable)"
  fi
fi

if [ "$FAIL" -ne 0 ]; then
  log "$FAIL check(s) failed"
  exit 1
fi
log "all present-only-available checks passed"
exit 0
