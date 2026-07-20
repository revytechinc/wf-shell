# Full-stack regression (Wayfire → tooling → Ly)

## What a regression test is

A previously observed failure, encoded as a **hard assert** that must **FAIL**
if that failure returns after rebuild / reinstall / restart.

It is **not** “smoke that something started.”

## Entry points

| Script | Scope |
|--------|--------|
| `scripts/full-stack-regression.sh` | Packages + Ly + session-launch sim + R1–R8 + units |
| `scripts/regression-full-reinstall.sh` | Stop stack → pkg verify → DRM restart → R1–R8 |
| `../ly/scripts/integration-freebsd-ly.sh` | Ly getty/wrapper/PAM/setup/session contracts |

```sh
# Live install verification + session restart
./scripts/full-stack-regression.sh

# After pkg rebuilds
./scripts/full-stack-regression.sh --reinstall

# Units + simulation only (no compositor kill)
./scripts/full-stack-regression.sh --units-only
```

## Stack packages

`revytech-wf-config` → `revytech-wayfire` → `revytech-wayfire-plugins-extra` →
`revytech-wf-shell` → `revytech-wcm` → `revytech-wf-osk` → `revytech-ly`

Rebuild via:

```sh
$PORTSDIR/revytech/wayfire/scripts/build-and-pkg-install.sh \
  x11-wm/revytech-wayfire x11/revytech-ly
```

## Known regressions encoded

### wf-shell (R1–R8)

See `scripts/regression-full-reinstall.sh` and `docs/VALIDATE-BEFORE-APPLY.md`.

### Ly FreeBSD (L1–L15)

- **L1–L2** gettytab `Ly:` → `ly_wrapper`; `ttyv1` uses `getty Ly`
- **L3** wrapper must not forward getty `login -fp` args
- **L4** PAM includes `login` (pulls `pam_xdg` → `/var/run/xdg/$USER`)
- **L5–L6** session desktops use absolute `wayfire-session-launch`
- **L7** `setup.sh` ends with `exec "$@"` (mock session simulation)
- **L8–L9** session-launch fail-closed on missing `XDG_RUNTIME_DIR`; anti-nest
- **L10** seatd enabled + running
- **L12** `config.ini` `tty=1` matches getty on `ttyv1`

### Product fixes shipped with this harness

1. **FreeBSD `getActiveTty`** — was `FeatureUnimplemented` → wrong VT / log spam
2. **`wayfire-session-launch`** — set seatd + clear nested `WAYLAND_DISPLAY` +
   fail-closed runtime dir
3. **Ly port** — embed `default_tty=1` / `fallback_tty=1` for FreeBSD getty
4. **XDG runtime** — probe `/var/run/xdg/$USER` when `/run/user/$uid` missing

## Unit coverage (wf-shell)

```sh
meson test -C build --print-errorlogs
```

Includes apply-gate refuse matrices and settings startup-gate fail-closed tests.

## Rule of thumb

If you fix a crash or dual-client bug, add an assert **before** declaring green.
If the suite cannot fail when the bug returns, it is not a regression test.
