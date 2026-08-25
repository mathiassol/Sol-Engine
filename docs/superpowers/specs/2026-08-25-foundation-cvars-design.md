# Config / cvars (Foundation #8)

Date: 25 Aug 2026  
Status: spec

## Decision

Named knobs live in `core`, layer 0, next to `log` / `profile` / `clock`. The
registry parses config **text**; it never opens a file. The engine reads the
bytes through `platform::IFileSystem` and hands the text down, so core keeps
zero OS dependencies and the dependency graph gains no edge.

```
packages/core/include/engine/core/cvar.hpp
packages/core/src/cvar.cpp
```

```cpp
enum class CvarType   : u8 { Bool, Int, Float, String };
enum class CvarSource : u8 { Default = 0, File = 1, CommandLine = 2, Code = 3 };

class Cvar {                    // static storage duration, self-registers, non-copyable
public:
    Cvar(const char* name, bool         value, const char* help);
    Cvar(const char* name, i32          value, const char* help);
    Cvar(const char* name, f32          value, const char* help);
    Cvar(const char* name, const char*  value, const char* help);

    const char* name() const;
    const char* help() const;
    CvarType    type() const;
    CvarSource  source() const;

    bool             as_bool() const;
    i32              as_int() const;
    f32              as_float() const;
    std::string_view as_string() const;

    bool set(std::string_view text, CvarSource source);
};
```

A knob is declared next to its reader:

```cpp
namespace { engine::Cvar cv_vsync{"r.vsync", true, "Present with vsync"}; }
```

Registry as free functions, the same shape as `set_logger` / `profiler`:

```cpp
Cvar* find_cvar(std::string_view name);   // nullptr when unknown
usize cvar_count();
Cvar* cvar_at(usize index);               // listing, for Debug #7's console

struct CvarApplyStats { usize applied = 0; usize unknown = 0; usize invalid = 0; };

CvarApplyStats apply_cvar_text(std::string_view text, CvarSource source);
CvarApplyStats apply_cvar_args(int argc, const char* const* argv);
```

The backing store is a function-local static vector, so static-init order
across translation units is safe. The registry is **not thread-safe**: write
it from startup and the main thread only.

Names are dotted lowercase groups: `r.*` renderer, `window.*` window,
`gate.*` reserved for the gate.

## Precedence

One rule: a write from source `S` is accepted only if `S >= current source`.

```
Default (0) < File (1) < CommandLine (2) < Code (3)
```

That single comparison is why the engine may load `config.cfg` **after** the
command line and `--set` still wins — no future reordering of startup can
silently flip precedence. Runtime `Code` writes (Build #10's options menu,
Debug #7's console) always win, including over a previous `Code` write.

Reading a cvar as the wrong type is a programming error, not a config error:
it asserts and returns a zero value.

## File format

`<content_root>/config.cfg`. Line-based, one knob per line:

```
# comment (also //)
r.vsync 0
window.mode = borderless
r.aa fxaa
```

Blank lines and comments are skipped. `key value` and `key = value` both
parse. An unknown key or an unparseable value is **warned and counted**, not
fatal — one typo must not stop the engine booting. A missing file is silence,
not an error.

Repo layout puts the file at the repo root; install layout puts it next to
`game.exe`. Both are correct, and both are the user's local preferences, so
`config.cfg` is added to `.gitignore`.

## Command line

`--set key=value` and `--set key value`, applied with `CvarSource::CommandLine`.
Arguments that are not `--set` are ignored, so `--gates` keeps its existing
hand-rolled parse and this stays a knob overlay, not a general argument
parser. Unknown keys warn and count `unknown`. A trailing `--set` with no
value, or a `--set` whose value is the next `--set`, counts `invalid`.

## Who loads what, when

1. `main` calls `apply_cvar_args(argc, argv)` where `--gates` is parsed today,
   before `Engine::init`.
2. `Engine::init` moves `create_filesystem()` and content-root discovery
   **above** `create_window` — neither depends on the window — then loads
   `config.cfg` through `IFileSystem`, then overlays `window.width`,
   `window.height`, `window.mode` and `r.vsync` onto `config.window` before
   the window is created. One log line names the counts and the window knobs
   actually applied: `Cvars: file=N cli=N window=1280x720 windowed vsync=on`.
3. `r.aa` is read by the sandbox at demo setup, because the AA default is demo
   state, not engine state.

`EngineConfig` keeps every field it has. Those stay the code-level defaults;
cvars overlay them.

## Wired knobs

| cvar | type | default | read by |
|------|------|---------|---------|
| `window.width` | i32 | 1280 | `Engine::init` |
| `window.height` | i32 | 720 | `Engine::init` |
| `window.mode` | string | `windowed` | `Engine::init` |
| `r.vsync` | bool | true | `Engine::init` |
| `r.aa` | string | `off` | sandbox demo setup |

Two inline parsers land alongside the existing `*_name` helpers, symmetric
with them and adding no dependency:

- `platform::parse_window_mode` in `platform/window.hpp`
- `renderer::aa::parse_mode` in `renderer/aa.hpp`

## Gate

The gate asserts against dedicated `gate.bool` / `gate.int` / `gate.float` /
`gate.string` cvars, so it never disturbs a knob the running app reads.

`Cvar gate: count=N text=yes types=yes precedence=yes file=yes missing=yes (pass)`

1. **registry** — every declared knob is found by name; an unknown name
   returns `nullptr`; `cvar_count()` is non-zero and `cvar_at` walks them.
2. **text** — a literal blob with comments, `key value`, `key = value` and
   blank lines produces the exact expected `applied` / `unknown` / `invalid`
   counts.
3. **types** — bool, i32, f32 and string round-trip through `set` and the
   typed accessors; `gate.bool = maybe` is rejected and counted `invalid`.
4. **precedence** — a `CommandLine` write followed by a `File` write of the
   same key keeps the command-line value; a `Code` write after both wins.
5. **file** — a temporary `.cfg` written through `IFileSystem` and loaded
   through the engine's own loader lands its value.
6. **missing** — loading an absent path reports no error.

## Visible check

```
sandbox.exe --set window.mode=borderless --set r.aa=fxaa
```

starts borderless with FXAA already on; F11 and F5 still cycle from there.
A `config.cfg` holding `r.vsync 0` reports as vsync off in the startup log —
the F3 overlay carries no present-interval slot, and this row does not add
one.

## Not this

- In-engine console (Debug #7) and options menu / quality presets (Build #10).
  This row is the registry those two consume, nothing more.
- Change callbacks or dirty flags. Consumers poll their cvar.
- Saving cvars back to disk.
- Environment variables. `ENGINE_GPU_DEBUG` is read inside the D3D12 and DXC
  backends before any registry exists; it stays as it is.
- `Vec3` or array cvars — `math` sits above `core`, so that would invert the
  dependency direction.
- Cheat / readonly / needs-restart flags.
- A general command-line parser.
