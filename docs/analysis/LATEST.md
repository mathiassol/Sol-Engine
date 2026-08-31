---
run: 2026-08-31
commit: 9da498c598a7e3416f35ef0f3f446bf2f42396bf
artifact: https://claude.ai/code/artifact/b33821a2-a764-4a21-b4c9-c152ccb4da26
---

# Sol Engine — where it stands

Sol Engine is a small Windows game engine whose structure is better than most
commercial engines and whose feature set is a fraction of theirs. The one real
defect from the last audit — lighting arithmetic done on the wrong numbers — has
been fixed and is now guarded by a self-check; what remains is absence rather
than error.

| What was graded | Grade | In one sentence | Detail |
|-----------------|-------|-----------------|--------|
| Stability | **B+** | Failures degrade and get logged nearly everywhere, and the one deliberate crash policy is honoured in practice — but a crash in a shipped game leaves nothing on disk. | `metric-stability.md` |
| Architecture | **A−** | The layering is real, checked automatically, and the same design Godot had to retrofit — but it has only ever been tested against one graphics backend. | `metric-architecture.md` |
| Capabilities | **C+** | Everything present works and is tested; character animation, on-screen text, compressed textures and multi-core work are all absent. | `metric-capabilities.md` |
| Cross-platform readiness | **B** | Windows-only code is genuinely confined to five packages, but the build will not even start on Linux because of one missing line. | `metric-portability.md` |
| Developer setup | **B+** | Every documented command worked first try, including installing and running the shipped build — but the code formatter's config disagrees with 86% of the code and nothing says so. | `metric-devex.md` |
| AI tooling | **B+** | The instructions are exact, and several of their numbers are enforced by the compiler itself — but they say nothing about the one config that would rewrite the whole tree. | `metric-ai-tooling.md` |

## What this means

**The structure is the asset, and it is genuinely uncommon.** The rendering
code never touches Direct3D — Windows' graphics interface — directly; it only
ever sees a neutral layer in between. A script verifies that across 140 files on
every push. The payoff is that adding a Linux or Mac graphics backend later
means writing new code beside the old rather than rewriting the renderer. Godot
arrived at the same design, but only by pulling apart a finished
Vulkan implementation while adding a second one, and it published how many
thousands of duplicated lines that split saved. Sol has the design before its
second backend. The honest caveat is the same fact stated the other way: no
second backend has ever tested it.

**Nothing here is wrong; a great deal is missing.** The last audit's one real
defect — lighting maths done on display-bent colour values instead of raw
ones — is fixed, with a check that will catch it coming back. What is left is
absence: no character animation, no text on screen, no compressed textures, and
no way to use more than one processor core. Those are whole subsystems, not
rough edges, and every engine Sol measures itself against has all four.

**One line blocks the stated goal.** The project aims at Linux and macOS. A
single unguarded line in a build file names a Windows-only component
unconditionally, so on Linux the build stops before one file is compiled.
Nothing would catch it: the automated checks do not examine this, and the
build test runs only on Windows. It is roughly half an hour of work, and it is
step one of everything else.

## What games this can build today

A single-room 3D physics puzzle or a short walking-sim works today, and is the
shape the sandbox already is — rigid bodies, a character controller, three
cameras, realistic shadowed lighting, sound effects.

Two shapes are out of reach for one reason each. A text-heavy RPG is
impossible: the only letters the engine can draw are twelve hand-typed
8-pixel bitmaps made for the debug display. A 200-enemy action game fails twice
over — 256 physics bodies in total, and no character animation.

A 2D platformer needs three pieces: text, see-through sprites, and texture
memory, which today means 22 MB per 2048-pixel image because nothing is
compressed.

## The three things worth doing next

1. **Guard the one build line, and add a check that finds its kin.** Wrap the
   Windows-only reference in `cmake/EngineRuntimeApp.cmake` the way the four
   packages beside it are already wrapped, then add an automated check that
   cross-references every such link against whether that package exists on this
   platform. Half an hour, and it stops the cross-platform goal failing at step
   one.
2. **Text on screen.** It is already marked ready to start, and it is the single
   absence that blocks the most game shapes — no menu, no score, no dialogue,
   no HUD without it.
3. **Compressed textures.** Carried over unfixed from the last audit. One
   2048-pixel texture costs 22 MB of graphics memory today and would cost under
   6 MB compressed. This is the ceiling on how much art any real game can ship.

## Since last time

Four of six grades rose, and the largest jump — stability, from D+ to B+ — is
because the colour-space defect that capped it two days ago has been fixed and
gated, not because anything else changed. One new problem was found that the
last pass missed: the build cannot configure on Linux.
