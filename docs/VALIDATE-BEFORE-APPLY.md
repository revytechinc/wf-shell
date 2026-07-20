# Permanent rule: validate before apply

**Owner expectation (never forget):** Every change that touches disk, the panel,
or the compositor must **validate first**, then apply. Fail soft — refuse and
log; never crash the session.

## Gate order

1. **Preflight** — env / sockets / files exist and are the right type.
2. **Probe** — can we load/parse (CSS via GTK, modes via advertised list)?
3. **Write** — only after probe succeeds.
4. **Live effect** — only after write is consistent (or intentionally live-first
   for displays *after* mode is known safe).
5. **Debounce** — continuous UI controls must not thrash reloads.

## Implementation

| Area | Gate |
|------|------|
| Theme CSS | `validate_theme_css_path` / `validate_theme_apply` in `apply-gate.*` |
| Theme write | `apply_theme_pack` refuses unknown/unloadable paths |
| Panel CSS reload | Stage providers → swap; never clear-first |
| Config inotify | Debounced reload; CSS only if `css_path` changed |
| Display modeset | Session socket + mode in hardware list + `wlr-randr` present |
| Settings UI | Live save debounced; panel `save()` re-validates theme |

## Debug

```sh
export WF_SHELL_DEBUG=1    # or WF_SETTINGS_DEBUG=1
wf-panel … 2>&1 | tee /tmp/wf-panel.log
wf-settings 2>&1 | tee /tmp/wf-settings.log
```

Look for `wf-shell:gate[...]` lines: `ALLOW` vs `REFUSE`.

## Integration tests (run every iteration)

```sh
# unit + product rules (never present what is not built/installed)
meson test -C build panel-capabilities-test display-config-test css-load-test --print-errorlogs
meson test -C build --print-errorlogs   # full suite

# installed tree: themes have menu icons; no weather if unbuilt
./scripts/integration-present-only-available.sh

# live seat: single panel, no Invalid widget, settings does not kill wayfire
./scripts/integration-live-session.sh
```

**Never present what is not there** — Settings catalog and theme discovery
must filter by `panel_widget_available()` / `theme_artifacts_complete()`.

## Why this exists

Skipping validation caused: compositor kills (wlr-randr / bad probes), panel
death on rapid theme thrash (`Error reading events from display`), and
persisted bad `css_path` values. **Everything gets validated.**
