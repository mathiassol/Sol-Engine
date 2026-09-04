---
raised_by: audit 6dd4ef1 (2026-09-02)
carried_from: docs/analysis/PLAN.md, which /aim-audit overwrites
status: open — not yet on the service
---

# Open decisions

Ten questions an audit raised and deliberately would not answer, because
answering one changes behaviour, an API, or what a word in the backlog means.
Each is the decision itself, not a task: the fix is clear in every case.

**Why this file exists.** The service owns decisions now — `tools/aim.ps1
decisions` is the live list, and an audit raises them there. These nine predate
that: the audit that found them ran at `6dd4ef1`, two commits before the
decision-list capability existed, so it submitted grades and findings but no
`decisions[]`. They lived in `PLAN.md`, which every audit overwrites.

**This file is temporary.** The next `/aim-audit` will re-raise whichever of
these are still true, on the server, where they belong. Delete this file then —
do not maintain both. Until that happens, this is the only copy.

Answer one with `pwsh -NoProfile -File tools/aim.ps1 decisions answer <code>
--note "..." --ref <where>` once it is on the server, or just decide it here and
say so in the commit.

---

## A3 / C1 — Should a scene instance be a handle instead of an array index?

**Why it is not a task:** it changes the scene's public API and ripples through
the four structs instance data is copied through, plus the motion history.

The scene is append-only. There is no `remove_instance`, `destroy_instance` or
`clear`, so nothing can be despawned; the only removal is replacing the whole
`World`. Compacting the array as a workaround is unsound, because `motion.hpp:25`
keys `prev_model[512]` by the instance index, so closing a hole silently repoints
every surviving instance at another object's previous transform.
`packages/physics` already solves this correctly with a handle carrying a
generation and a live flag per slot.

Do you want the scene to adopt that model — the single change that most widens
what can be built on this engine — and if so, is it a design spec and a Ready
row rather than a plan task?

## S1 — Should `intern_name` return a sentinel instead of aborting?

**Why it is not a task:** a call that currently aborts would start returning 0.

`world.hpp:79` states the rule for full containers: return the invalid sentinel
and log, because "how many instances a game spawns is a content outcome, not a
programmer error". `intern_name`, declared four lines below, calls
`ENGINE_ASSERT_MSG` instead, and `ENGINE_ASSERT` has no `NDEBUG` guard — so the
shipped player aborts once 513 distinct names have ever been interned. Reachable
by renaming, not by spawning.

Should `intern_name` follow the rule its own header states, and should the gate
that proves it be written first?

## S2 — Should an over-long name be rejected rather than shortened?

**Why it is not a task:** a call that currently succeeds with a truncated name
would start failing.

`intern_name` copies `min(name.size(), 31)` with no log, so two names differing
only after the 31st character collide into one identifier and lookup returns the
wrong instance. The content path is already safe — `scene_file.cpp:158` rejects
an over-long name with a positioned parse error and `prefab.cpp:143` rejects when
prefix plus name would overflow — so only direct API calls reach it.

Reject and log to match the content path, or document the cap as part of the API
contract?

## S4 — Should success-returning functions be `[[nodiscard]]`, and warnings fatal?

**Why it is not a task:** which functions get the attribute is a style call, and
warnings-as-errors is a trade against toolchain upgrades.

18 public functions return a bool meaning success; 3 declarations in the whole
public surface are marked, all in `arena.hpp`. Warnings are on at `/W4` but not
as errors. The discipline currently holds — no call site in the engine packages
drops a result, and a clean rebuild of renderer, core and scene produced 0
warnings — so this is about locking in a property the tree already has.

Mark all 18, mark only the ones with an output parameter, or leave it? And add
`/WX` now that the count is zero?

## A1 — Who should own the development-tree marker?

**Why it is not a task:** it changes `EngineConfig` or the detection contract.

`packages/engine/src/engine.cpp:44` decides it is running from a development tree
by testing for `packages/sandbox/content/test.txt` — layer 5 naming a layer 6
application package, as a string, which is why the downward-dependencies
invariant cannot see it. A game built on Sol that renames the sandbox loses
development-tree content resolution.

Should the application supply the marker through `EngineConfig`, or should the
engine test for a generic marker it owns — and should the invariant grow a check
for `packages/<name>` literals in lower layers?

## A2 — Should the scene-to-renderer extract move into an engine package?

**Answered 4 Sep 2026: yes, move the extract itself.** It is now
`packages/scene-render`, a Layer-4 package at rank 5 (`852ceee`), and both
`static_assert`s moved with it, so a game linking `engine::scene-render`
inherits the guards instead of having to write its own extract. The constants
stayed where they were — moving the extract made a shared header unnecessary.
The text below is the question as the audit raised it.

**Why it is not a task:** it changes the application-facing shape of the
renderer, and there is more than one reasonable boundary.

No engine package bridges the scene to the renderer; the bridge is
`packages/sandbox/src/world_extract.cpp`, 160 lines of application code. Two
`static_assert`s in that file are the only thing coupling
`scene::kMaxPointLights` to `renderer::kMaxPointLights` and `kMaxInstances` to
`motion::kHistorySlots`, and the file's own comment says that if they diverge "it
is a buffer overrun with nothing to catch it". A game writing its own extract —
which it must, since the engine ships none — inherits no guard.

Move the shared constants into one header both packages include (cheap, removes
the overrun risk), move the extract itself (larger, gives a game a path it does
not have to write), or both?

## S5 — How should a material's textures be named in a `.solscene` file?

**Raised 4 Sep 2026** while implementing the material/texture system, not by an
audit. Decided the same day; recorded because it constrains the file format,
which `document` owns.

`scene::Material::albedo` was a `u32` index into a hardcoded husky/floor branch
in the bridge, and it is serialized — `scene_file.cpp:195` writes it,
`scene_file.cpp:305` parses it, and `run_scene_file_gate` asserted it survives a
round trip. Task 3 replaces it with three `TextureHandle`s, so the token has to
mean something else or nothing.

Serializing the handle is the option the file's own `MeshHandle` precedent
suggests (`scene_file.cpp:320` writes `id generation`), and it is wrong here. A
`TextureHandle` is `fnv1a64(path + colour space)`: writing it puts an opaque
20-digit number where a path belongs, in a file `VISION.md` says a human and an
agent must both be able to edit. The right format names the texture by path and
resolves it at load against a store — which needs the store at load time, which
is `document`'s job, which is why D5 of the design spec says "no `.solscene`
changes".

**Decided: keep the on-disk token shape, parse and discard it, always write
`0`.** Old files still load, `demo.solscene` is untouched, and the only thing
lost is the ability to persist an index into a branch this task deletes.

`run_scene_file_gate`'s `albedo == 2` clause is **replaced, not deleted**: the
loader must produce an *invalid* handle, so a future change that reinterprets
the token as an id goes red. That gate never asserted `metallic`/`roughness`
round-trip at all, so those clauses were added at the same time — one
meaningless assertion out, two real ones in.

**Open part:** when `document` lands, does the material line become
`albedo=/content/x.png` with load-time resolution, and does it pick up
`opacity`, which is *already* not serialized (`scene_file.cpp:192-198`)?

## D2 — How should a release be stopped from shipping ungated GPU code?

**Why it is not a task:** it changes what CI and the release workflow do, and may
mean new infrastructure.

50 of 88 gates are GPU gates that no hosted runner can execute, for reasons
documented in three places and not in dispute. `release.yml` states the
consequence itself: "a release is only as gate-verified as the last local
`--gates` run on the commit it was built from. Run them before you tag" — with
nothing enforcing it.

A self-hosted runner with a real adapter, a tag-time check that the commit has a
recorded local gate run, or accept it and keep the note?

## T3 — Is a feature "Done" when it works on one of two backends?

**Why it is not a task:** this is about what a word in the canonical backlog
means, and the backlog is mirrored by the service and read by the
`map-dependencies` invariant.

`docs/ENGINE_MAP.md` marks Platform #6, "Fullscreen / borderless / vsync control
on the interface", **Done**. Vsync control is not honoured on the Vulkan backend
— it takes IMMEDIATE regardless and warns — while RHI #25 carries that defect as
Ready. Both rows are individually accurate and jointly misleading: reading the
Platform category alone tells you vsync works.

Qualify the Done row with a clause naming the backend split, change what Done
requires for a two-backend feature, or leave the two rows to be read together?

## A4 — What should the "eight files" figure in CLAUDE.md say?

**Why it is not a task:** whether that number is a floor or a typical cost is a
judgement about what the guide is telling a contributor.

CLAUDE.md says a working pass "spans eight files — four structs to plumb through,
plus the shader, the pass registration, the recorder, and the gate", which reads
as a structural minimum and is a fair description of one. The most recent
renderer feature, Renderer #16, measured 23 distinct files across four commits,
19 of them code, spanning renderer, rhi-vulkan, scene, sandbox and the shader
content — because `opacity`, one float, is declared in three parallel structs and
hand-copied at two boundaries.

Keep eight as the minimum and add the measured cost of a real feature beside it,
replace it, or leave it?
