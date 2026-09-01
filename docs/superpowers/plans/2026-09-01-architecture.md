# Architecture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** close the three findings the 31 Aug audit left open against Architecture
— A2, A3 and A4 — so a fresh `/analizeMax` finds nothing in this dimension to
hold it below the band its own evidence already supports.

**Run this plan before [the stability plan](2026-09-01-stability.md).** Task 1
here builds the gate registry that plan's headless runner needs. Doing them in
the other order means writing the CPU/GPU split twice.

---

## Where this dimension actually stands

The audit graded Architecture **B+** (Solid, top of band), no ceilings fired,
and was explicit about what held it there:

> **A1** … This is the single criterion separating this dimension from
> Exemplary. Fixing it is a design job (a pipeline registry keyed by name, so
> the plumbing is one insertion instead of four), which is why it is on the
> Needs-a-decision list.

**A1 shipped today** — `FramePipelines` carried by value, a `kFramePipelines`
table, a `static_assert` tying the two together, and a completeness gate. The
pass-adding checklist went from eight files to five and from three hand-written
copy blocks to none. So the stated criterion is met, and what remains are three
findings the audit rated Medium, Medium and Low:

| ID | Finding | Sev | State |
|----|---------|-----|-------|
| **A1** | Pass path: eight files, three copy blocks, silent failure | Medium | **closed** — `4791726`…`b37f8ae` |
| **A2** | `GraphicsPipelineDesc` encodes a D3D12 root-parameter model in a backend-agnostic header | Medium | open |
| **A3** | Four refs per pass, and compute cannot be a graph pass | Low | open |
| **A4** | `main.cpp` is 26% of the engine | Medium | open, and **worse** |

**A4 has grown.** The audit measured 5,811 lines; it is **6,189** today and
2.4× the next-largest file, because this session added gates and guards to it.
Gate definitions occupy lines 769–5,180 — about 4,400 of those 6,189.

The band's own test — *"a specific thing here that a reference implementation
could learn from, and what it is"* — is already satisfied and the audit named
it: the `static_assert` family on GPU-facing struct sizes, which "bgfx and sokol
cannot do because they do not own shader-side structs". This plan does not need
to invent that evidence. It needs to remove what argues against it.

**This plan does not grade.** Only a fresh `/analizeMax` can.

---

## The two decisions, taken

**A4 — split the gates by domain.** All 72 move to
`packages/sandbox/src/gates/`, grouped by domain, behind a registry. Rejected:
extracting only the ~23 device-free gates (leaves `main.cpp` near 5,000 lines
and A4 open), and leaving the file alone (fastest to the stability win, but the
finding stays and the file keeps growing). CLAUDE.md's gate definition is
rewritten with the code — the audit flagged that as the reason this sat on the
Needs-a-decision list, and it is a documentation change, not an obstacle.

**A2 — make the debt explicit and checked, not redesigned.** The vocabulary
leaves the interface and the translation a second backend must perform is
written down as a contract, guarded by an invariant. Rejected: redesigning to a
bind-group model now. That *is* the industry-validated shape — WebGPU/Dawn chose
the Vulkan binding model deliberately, because Vulkan→D3D12 is the cheap
translation direction and D3D12→Vulkan the expensive one — but `rhi-vulkan` is a
**Far** row, so it would be designed and tested against exactly one backend,
which is how abstractions get the wrong seams. Also rejected: deferring
entirely, which leaves a finding a fresh audit will name again.

---

## Read this before Task 1 — the rules that will bite you

1. **A test is a gate.** A plain function asserting on real values, logging a
   line ending `(pass)` or `(FAIL)`. Write it, watch it fail, then implement.
   Task 1 moves 72 of them without changing what any of them asserts; Task 4
   adds one.
2. **Every commit is green.** `--gates` exits 0 with **75 `(pass)`, 0 `FAIL`**
   in Debug and Release (76 from Task 4), `check-invariants.ps1` passes
   **14/14** (15 from Task 2, 16 from Task 3).
3. **`docs/ROADMAP.md` is an implicit dependency of every source change.**
   `roadmap-audit` recounts C++/HLSL lines. Every task here moves them.
4. **Never run `clang-format`.** `format-hygiene` fails a push that breaks
   `.editorconfig` — 100 columns, LF for sources, CRLF for `.ps1`.
5. **The renderer never includes a graphics-API header, and no app registers an
   engine pass.** Task 5 adds a pass kind to the graph; it goes in
   `packages/renderer`, and `no-app-render-passes` will catch it if it does not.
6. **Trunk-based.** Commit to `main`, push after each task.

### The verification block

```powershell
cmake --build --preset debug
.\build\bin\Debug\sandbox.exe --gates
$env:ENGINE_GPU_DEBUG=1; .\build\bin\Debug\sandbox.exe --gates; $env:ENGINE_GPU_DEBUG=$null
cmake --build --preset release-game
.\build\bin\Release\game.exe --gates
pwsh -NoProfile -File tools/check-invariants.ps1
```

Use `pwsh`, not `powershell` — 5.1 needs `-ExecutionPolicy Bypass`.

---

## Task 1: One gate signature, and a context to feed it

**From:** A4 · **Cost:** half a day · **Prerequisite for everything after it**

72 gates have 72 different signatures — `()`, `(const ForwardDemo&)`,
`(IDevice&, IShaderCompiler&)`, `(IPhysics*)`, and so on. That is what makes
them impossible to put in a table, and a table is what Task 2 and the stability
plan both need.

- [ ] **1.1** Add `packages/sandbox/src/gate_context.hpp`: a `GateContext`
  holding non-owning pointers to everything any gate currently takes — device,
  shader compiler, asset loader, window, input, audio, physics, the `World`, the
  `FlyCamera`, the `ForwardDemo`, the `Engine`, the content layout, the scratch
  directory. Every member nullable, and documented as nullable: a headless run
  fills in a fraction of it.
- [ ] **1.2** Change every gate to `bool run_x_gate(const GateContext&)`.
  Mechanical: each body starts by pulling what it used to take from the context.
  A gate that needs something absent must return early with a logged reason —
  **not** dereference a null pointer, and **not** silently pass.
- [ ] **1.3** Update the ~72 call sites in `run_app` to pass the context.

**Proof:** the verification block, **75 `(pass)`, 0 `FAIL`** in both
configurations, byte-identical gate names and counts. This task must change no
assertion — if the pass count moves, something was dropped.

**Commit:** `refactor(sandbox): one gate signature, fed by a GateContext (analizeMax A4)`

---

## Task 2: The gate registry, and an invariant that it is complete

**From:** A4 · **Cost:** 2–3 hours

Same idea as A1's `kFramePipelines`, applied to gates: one table, and a
machine-check that nothing is missing from it. A1 could use a `static_assert` on
struct size; this cannot, so the completeness check is an invariant instead.

- [ ] **2.1** Add a `GateEntry { const char* name; GateKind kind; bool (*fn)(const GateContext&); }`
  and a `kGates[]` table. `GateKind` is `Cpu` or `Gpu` — **data, not two
  hand-maintained sequences**, which is the whole point: the headless runner in
  the stability plan filters on it, and nobody has to keep two lists in step.
- [ ] **2.2** Replace the hand-written call sequence in `run_app` with a loop
  over `kGates`, preserving order. Keep the `if (!x) { ok = false; }` shape — no
  `&&`, which is finding S3 and is already fixed.
- [ ] **2.3** Add invariant **`gate-registry`**: every `bool run_*_gate(` defined
  under `packages/sandbox/src/` appears exactly once in `kGates`, and every
  `kGates` entry names a defined gate. Source-only, no compiler — this is the
  check that makes the table trustworthy, and it is the direct analogue of the
  `static_assert` the audit credited.
- [ ] **2.4** Control-test it both ways: add a gate and omit it from the table
  (must fail naming the gate), and add a table entry for a gate that does not
  exist (must fail naming the entry). Revert each.
- [ ] **2.5** Update `CLAUDE.md`'s check count 14 → 15 and its enumeration.

**Proof:** 75 `(pass)` unchanged; `all 15 checks passed`; both control cases
watched failing.

**Commit:** `feat(sandbox): a gate registry, and an invariant that it is complete (analizeMax A4)`

---

## Task 3: Move the gates out of `main.cpp`

**From:** A4 · **Cost:** half a day

- [ ] **3.1** Move `ForwardDemo`, `SandboxState` and `FlyCamera` into headers
  (`forward_demo.hpp`, `sandbox_state.hpp`) so a gate TU can see them. These are
  app-internal, so they stay under `packages/sandbox/src/` — `header-layout`
  only governs `include/engine/<domain>/`, and putting app types there would be
  wrong.
- [ ] **3.2** Move all 72 gates into `packages/sandbox/src/gates/` by domain:
  `core`, `scene`, `assets`, `renderer`, `rhi`, `physics`, `platform`. Add each
  new `.cpp` to `engine_add_runtime_app`'s source list so both `sandbox` and
  `game` build them.
- [ ] **3.3** Leave in `main.cpp`: the demo world, pipeline creation, input, the
  frame callbacks, `run_app` and the exception boundary. Expect roughly 1,800
  lines, down from 6,189.
- [ ] **3.4** Rewrite CLAUDE.md's "What a gate is": a gate is a function in
  `packages/sandbox/src/gates/<domain>.cpp` taking a `const GateContext&`,
  registered in `kGates`, and the rules that make one worth having are unchanged.
  Update `.claude/rules/renderer-boundaries.md` step 6 to match.
- [ ] **3.5** Update `docs/ROADMAP.md`'s LOC audit — the total barely moves, but
  the per-package sentence naming `sandbox` at 30% needs its figures recounted.

**Proof:** the verification block; **75 `(pass)`, 0 `FAIL`**, Debug and Release;
`all 15 checks passed` including `gate-registry`; `doc-links` still resolves.
Then a timing check worth having: touch one gate file and rebuild — it should
compile one TU, not 6,000 lines.

**Commit:** `refactor(sandbox): gates move to per-domain files, main.cpp drops to ~1,800 lines (analizeMax A4)`

---

## Task 4: A compute pass the graph can schedule

**From:** A3 · **Cost:** half a day

`PassKind` is `{Graphics, Copy}`. The RHI implements compute — `RHI impl gate:
compute=yes dispatch=yes` — and RHI #6 is Done, so `create_compute_pipeline` and
dispatch both work and buffer UAVs already work. The graph simply cannot express
one, so bloom's chain is fullscreen triangles rather than dispatches and a
compute pass cannot participate in dependency tracking.

- [ ] **4.1** Add `PassKind::Compute` and a compute recorder alongside the
  graphics one, in `packages/renderer` — declared in `render_snapshot.hpp`,
  defined in `render_graph.cpp`, per the renderer-boundaries checklist.
- [ ] **4.2** Make `compile()` treat a compute pass's reads and writes exactly
  as it treats a graphics pass's, so ordering, missing-producer detection and
  cycle detection all cover it. This is the finding: not "compute exists" but
  "compute participates".
- [ ] **4.3** **Write the gate first and watch it fail.** `run_compute_pass_gate`
  registers a compute pass writing a buffer and a graphics pass reading it, and
  asserts the graph orders them; then asserts a missing producer for a
  compute-read resource is reported by name; then that a cycle through a compute
  pass is detected. Assert on the ordering the graph produces, not on "it
  compiled". 76 gates after this.
- [ ] **4.4** Leave `kMaxRefs = 4` alone and say why in a comment: the highest
  count in use is 3, `add_pass` clamps and logs above the cap (S4, fixed), and
  raising a limit nothing has reached is speculation.

**Proof:** 76 `(pass)`, 0 `FAIL`, Debug and Release. Gate watched failing before
the recorder exists. `ENGINE_GPU_DEBUG=1` silent — this dispatches real work.

**Commit:** `feat(renderer): compute passes participate in the render graph (analizeMax A3)`

---

## Task 5: Get D3D12 out of the RHI's vocabulary, and check it stays out

**From:** A2 · **Cost:** half a day

Measured today, the leak is in three of the four public RHI headers:

```
commands.hpp:58-62   "Backed by a *root* SRV, not a descriptor table … 2 DWORDs
                      of root signature … register space 1 … t0.. registers"
resources.hpp:122    "Root SRVs in register space 1, visible to all stages
                      (t0..tN, space1)."
device.hpp:23        "a DXGI adapter query about video memory"
device.hpp:41        "Packed like the D3D12 enums (no graphics API in this header)."
```

`rhi.hpp:12`'s `D3D12` enumerator is a backend *name* and stays.

- [ ] **5.1** Rename the counts to API-neutral terms, carrying the same
  semantics: `constant_buffer_count` → `uniform_buffer_count`,
  `shader_resource_count` → `sampled_texture_count`, `structured_buffer_count` →
  `storage_buffer_count`, `unordered_access_count` → `storage_texture_count`.
  `sampler_count` is already neutral. Mechanical rename across `rhi`,
  `rhi-d3d12`, `renderer`, `debug-draw` and the sandbox.
- [ ] **5.2** Replace those comments with **one mapping table** in
  `resources.hpp`: each count, what D3D12 makes of it, and what a Vulkan backend
  must synthesise. That is the contract a second backend implements, and writing
  it down is the difference between known debt and a trap. Keep the *reasoning*
  the current comments carry — why a root SRV is vertex-visible where a table is
  not is real, portable information about binding models; it just needs saying
  without `t0`.
- [ ] **5.3** Add invariant **`rhi-vocabulary`**: no `D3D12`, `DXGI`, `SRV`,
  `UAV`, `CBV`, `root signature`, `register space` or `tN`/`bN`/`sN` register
  syntax anywhere under `packages/rhi/include/`, with a short allowlist —
  `rhi.hpp`'s backend enumerator, and the mapping table itself, which names
  D3D12 on purpose. `graphics-api-isolation` already checks *headers*; this
  checks *vocabulary*, which is what leaked.
- [ ] **5.4** Control-test it: put `register space 1` back in a comment, watch
  it fail naming file and line, revert.
- [ ] **5.5** Update `docs/ARCHITECTURE.md`, which currently concedes the leak
  ("a Vulkan backend must synthesize one"), to point at the mapping table
  instead of restating the problem. Update CLAUDE.md's count 15 → 16.

**Proof:** `all 16 checks passed`; control case watched failing; the
verification block green — a rename touching four packages must change no
behaviour, so 76 `(pass)` and a silent debug layer are the assertion.

**Commit:** `refactor(rhi): a binding contract in neutral vocabulary, machine-checked (analizeMax A2)`

---

## Task 6: Record it

**Cost:** 30 minutes

- [ ] **6.1** A `docs/ROADMAP.md` Why / Choice / Gate / Do-not entry for the
  gate registry and split, the compute pass kind, and the RHI vocabulary
  contract. The **Do-not** lines matter most: do not put gates back in
  `main.cpp`; do not raise `kMaxRefs` without a pass that needs it; **do not
  redesign the binding model to bind groups until `rhi-vulkan` exists to
  validate it** — and say that Dawn's Vulkan-shaped choice is the expected
  answer when that day comes, so the research is not redone.
- [ ] **6.2** Flip RHI #9's *Finish first* if Task 4 changed what it waits on —
  it currently reads "A compute pass that **writes** a texture", and a compute
  pass that writes a *buffer* now exists.

**Commit:** `docs(roadmap): the gate registry, compute passes, and the RHI contract`

---

## Definition of done

- [ ] `main.cpp` is under 2,000 lines and holds no gate
- [ ] all 72 gates take `const GateContext&` and appear in `kGates`
- [ ] `gate-registry` and `rhi-vocabulary` invariants exist, and each has been
      watched failing on an injected violation
- [ ] `all 16 checks passed`, under PowerShell 7 and 5.1
- [ ] **76 `(pass)`, 0 `FAIL`** in Debug and Release; debug layer silent
- [ ] a compute pass participates in graph ordering, missing-producer detection
      and cycle detection, proven by a gate watched failing first
- [ ] no `D3D12`/`DXGI`/`SRV`/`UAV`/register-space vocabulary under
      `packages/rhi/include/` outside the allowlist
- [ ] touching one gate file rebuilds one translation unit
- [ ] `docs/ROADMAP.md` carries the entry and its audit line matches the recount
- [ ] CI green on every job, including the Linux one

## What this plan does not do

No `rhi-vulkan` (RHI #12, **Far**) and no bind-group redesign — see the decision
above. No UAV *textures* (RHI #9, **Later**): Task 4 gives the graph a compute
pass over buffers, which is what the RHI supports today. No change to
`kMaxRefs`. No move of gate code out of `packages/sandbox` — `game` reuses those
sources and a separate test package would need its own answer to that.
