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

## Regression suite (this is what “full reinstall” means)

A **regression test** encodes a bug that already bit us. It must **FAIL** if
that bug returns. Smoke that “something started” is not a regression test.

```sh
# FULL: stop stack → verify package on disk → restart DRM session →
# assert R1–R8 (dual panel, weather widget, settings-kills-wayfire, missing
# theme icons, pkg ownership, …) → unit suite
./scripts/regression-full-reinstall.sh
# log: /tmp/wf-shell-regression.log
```

Known regressions asserted by that script:
- **R1** dual wf-panel / dual background  
- **R2** `Invalid widget: weather` (presenting unbuilt widgets)  
- **R3** wf-settings kills wayfire  
- **R4** theme CSS without menu icon  
- **R5** package missing files / checksum / old binary  
- **R6** panel dies when settings opens  
- **R7** no panel when wayfire is up  
- **R8** key files not owned by `revytech-wf-shell`

### Smaller suites (not a substitute for full regression)

```sh
meson test -C build --print-errorlogs
./scripts/integration-present-only-available.sh
./scripts/integration-live-session.sh
```

**Never present what is not there** — Settings catalog and theme discovery
must filter by `panel_widget_available()` / `theme_artifacts_complete()`.

## Why this exists

Skipping validation caused: compositor kills (wlr-randr / bad probes), panel
death on rapid theme thrash (`Error reading events from display`), and
persisted bad `css_path` values. **Everything gets validated.**
