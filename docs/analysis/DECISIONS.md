---
raised_by: audit 6dd4ef1 (2026-09-02)
carried_from: docs/analysis/PLAN.md, which /aim-audit overwrites
status: open — not yet on the service
---

# Open decisions

Twelve questions an audit raised and deliberately would not answer, because
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

## S6 — The sky pass painted over every opaque fragment, and no gate could tell (answered)

**Raised 4 Sep 2026** by importing the alley, which is the first scene with no
translucent material in it. Not an audit finding. **This is a live bug in
shipped code, not a design question** — it is here because the fix is a row,
not a character, and because the gate gap it exposes matters more than the bug.

The chain, each link verified:

| Fact | Where |
|---|---|
| Reversed-Z maps near to 1 and far to 0 | `math/mat4.hpp` |
| The sky vertex shader emits depth **1.0** | `sandbox/content/shaders/sky.hlsl:29` |
| The sky pipeline tests `depth_closer_or_equal` → `GreaterEqual` | `rhi/resources.hpp:134`, `sandbox_common.cpp:289` |
| The sky pass is registered **after** `forward` | `renderer/src/standard_frame.cpp:142` vs `:115` |
| Its blend is `Opaque` and its cull is `None` | `sandbox_common.cpp:291-292` |

So the sky writes at the *near* plane and passes `GreaterEqual` against every
opaque fragment in the frame, then overwrites it. The `transparent` pass is
registered after the sky (`:168`), which is the only reason anything has ever
been visible.

**Why it survived this long.** The husky demo makes exactly one of its four
material variants translucent (`main.cpp:933`, `opacity = 0.45f`), and those
~16 of 63 instances are what the window has been showing. The comment beside
that line claims the variants "end up scattered in front of and behind opaque
ones — which is what makes the depth test visibly do its job." The opaque ones
were never drawn. The checker floor quad has never rendered either.

**The gate gap is the real finding.** With the sky moved to the far plane the
whole suite still passes: **91 gates, 0 FAIL, exit 0, byte-identical verdicts**.
Nothing in this engine asserts that opaque geometry survives compositing. Gates
prove draws are *submitted* — batch counts, instance counts, readbacks of
vertex buffers — and the frame-loop gates prove frames complete. None reads a
composited pixel where geometry should be. That is the assertion to write
*before* touching the shader.

**Why the fix is not one character.** Changing `1.0` to `0.0` renders the alley
and makes all 63 huskies and the floor appear — that is how the diagnosis was
confirmed — but:

1. `depth_closer_or_equal` supports **both** conventions, so a hardcoded `0.0`
   is wrong under standard Z where far is 1.0. The shader needs the active
   convention's far value, the way `kSunDisk` is already a shader define.
2. `r.exposure` defaults to **-2.0 EV, picked by screenshot sweep** against a
   frame that was mostly sky (`sandbox_common.cpp:26-29` records the method and
   the luminance readings). Every one of those numbers was tuned around this
   bug, so fixing it changes what the demo is supposed to look like.
3. The compositing gate from the paragraph above does not exist yet.

Do these in that order: gate, then shader, then re-tune exposure with the
sweep repeated honestly.

**Answered 5 Sep 2026**, in that order. The full Why / Choice / Gate / Do-not
entry is in [../ROADMAP.md](../ROADMAP.md) — "S6 — the sky was painting over
every opaque fragment, and 91 gates agreed"; only what this file owes a reader
is repeated here.

**What the gate now asserts.** `run_sky_compositing_gate` (Gpu,
`gates_renderer.cpp`) drives the **real** sky pipeline and the shipped
`sky.hlsl` — not a stand-in, since the defect was a literal inside that shader
— into an `RGBA16_FLOAT` target, and reads one probe texel back as halves in
two conditions:

1. An occluder drawn at a near-ish depth, then the sky over it in the shipped
   order. The probe must still read the occluder's colour, exactly.
2. Nothing drawn, so depth is left at its clear value — the far plane. The
   probe must read the sky, not the clear colour.

**Both clauses are required**: without the second, "never draw the sky at all"
satisfies the first. The sky expectation is computed from the same ray
`direction_from_ndc` reconstructs and the same `ibl::sky_radiance` the cubemap
was baked from, rather than a remembered number; both backends measure 0.1%
error and read back byte-identical halves, so the tolerance is 2%. It failed
first, reading the sky's `39F1/3A8B/3B9B` where the occluder's `4000/4400/4800`
belonged, and passes on both backends now. `run_sky_gate` gained the CPU half:
`far_depth` under **both** conventions, not the live one.

The other two follow-ups are done with it. `rhi::far_depth(DepthConvention)`
now owns the expression that existed five times unowned across the two
backends, and `sky::Constants` carries the value in a named field with no
default argument on `make_constants`. The exposure sweep was repeated against
the fixed frame and the -2.0 EV default survived it on merit; the readings, old
and new, are in `sandbox_common.cpp`'s comment and in the ROADMAP entry.

## R1 — Should per-batch constants be one indexed array instead of one upload each?

**Raised 4 Sep 2026** while raising `kMaxInstances` for the alley. Not by an
audit. Not decided — this records the question and why the immediate fix was
something else.

`render_graph.cpp:181` allocates and writes a constant buffer *inside* the
batch loop, and three geometry passes — shadow, forward, motion — each do it.
That is 1,024 bytes of frame-ring upload per batch, against 144 bytes per
*instance* for `InstanceData`, which is already uploaded once for the whole
frame as one indexed array.

So the per-batch cost dominates as soon as a scene has many distinct meshes,
because two meshes cannot share a batch. The alley: 1,254 meshes behind 2,806
instances needs 1.61 MiB, of which 1.28 MiB is per-batch constants and only
0.4 MiB is instance data. `run_frame_ring_budget_gate` models the same thing at
the cap and is what made this visible.

The immediate fix was to grow `kFrameRingBytes` from 1 MiB to 8 MiB in both
backends, which is cheap (24 MiB of upload heap per backend) and buys 57%
headroom at `kMaxInstances = 3072`.

**The question this leaves open:** should per-batch constants become one
indexed array, the way `InstanceData` already is, with the batch index reaching
the shader the same way `instance_base` does? That would take `per_batch` from
1,024 bytes to roughly zero and make the ring's size independent of the
instance cap — which is the difference between an engine that scales to an open
world and one that grows a buffer every time the cap moves.

**Why it is not a task:** it changes the shader-side binding contract for
three passes and costs two backend implementations, and
`.claude/rules/renderer-boundaries.md` says a per-draw constant buffer is the
thing to avoid — so the answer interacts with a rule that would need rewording.
It deserves its own row, sized and gated on its own.

**A second, smaller part, surfaced by the 8 MiB change:** `kFrameRingBytes` has
**no owner**. It is defined once per backend — `rhi-d3d12/src/device_d3d12.cpp`
and `rhi-vulkan/src/device_vulkan.hpp` — and asserted a third time as a literal
in `run_parity_frames_gate`, which went red on the raise and is how the third
copy was found. Its comment now names all three sites, so the copy is loud and
gated rather than silent, but three copies of one number is what the docs rules
exist to prevent.

Should it move to one place? The ring is arguably part of the RHI contract
already — `IDevice::frame_ring_stats().capacity_bytes` is a contract method and
`run_frame_ring_budget_gate` assumes a single number across backends. Against
that: a backend could legitimately want a different size for its own allocator,
and making it shared is itself a contract change. Either resolve it (one
`constexpr` in `rhi`, both backends and the gate referring to it) or make the
per-backend freedom real and stop asserting one value — the present state
claims both.

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
