# Shell JSON config — TAOCP load/save contract

## Paths

| Role | Path |
|------|------|
| Primary | `~/.config/wf-shell/config.json` (or `$XDG_CONFIG_HOME/wf-shell/config.json`) |
| Last working | `config.json.last-good` (sibling) |
| Quarantine | `~/.config/wf-shell/quarantine/config.json.bad-<stamp>` (+ `.reason.txt`) |

Override primary: `WF_SHELL_JSON_CONFIG=/path/to/file.json`

## Schema (v1)

```json
{
  "version": 1,
  "panel": { "position": "top", "...": "string values" },
  "background": { },
  "dock": { },
  "locker": { },
  "mcp": {
    "enabled": false,
    "servers": [ { "id": "...", "name": "...", "enabled": false, ... } ]
  }
}
```

- **Unknown root keys**: ignored (soft warning).
- **Unknown keys inside section objects**: stored as strings when scalar; non-scalars skipped.
- **Unknown option names** when applied to live config: skipped (XML catalog is truth).
- **Hard fail**: not JSON, root not an object, empty file.

## Load (boundary)

1. Read primary → **validate** → parse.  
2. If hard fail: **quarantine** primary for inspection.  
3. Try **last-good** (validate + parse).  
4. If that fails: quarantine last-good; write **baseline** (position top, MCP stubs).  
5. Missing primary with no last-good: first-run (`source=missing`); Settings creates baseline.

## Save (boundary)

1. Serialize in-memory config.  
2. **Validate** serialized text; refuse write if invalid.  
3. Atomic write (temp + rename).  
4. Re-read disk → **validate**; on failure restore last-good if present.  
5. On success: copy primary → **last-good**.

## Pure core vs boundary

| Pure (no I/O) | Boundary (I/O) |
|---------------|----------------|
| `validate_shell_json_text` | `load_shell_json_config_resilient` |
| `parse_shell_json_config` | `save_shell_json_config` |
| `serialize_shell_json_config` | `write_baseline_shell_json` |
| `make_baseline_shell_json` | quarantine / promote last-good |

## Related code

- `src/util/shell-json-config.{hpp,cpp}`
- Tests: `tests/shell-json-config-test.cpp`
- Consumers: panel `wf-shell-app` reload, Settings `settings_save_section`
