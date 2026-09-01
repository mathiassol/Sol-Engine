---
run: 2026-08-31
commit: 2a92289a6e8e79e11486c9e73301b138212a3e8c
artifact: https://claude.ai/code/artifact/b33821a2-a764-4a21-b4c9-c152ccb4da26
---

# Sol Engine — where it stands

Sol Engine builds clean and passes all 71 of its own self-checks in both the
development and the shipping build, with a structure most commercial engines
would envy. What it still cannot do is let anyone build a game that has words on
the screen, characters that move, or a level stored in a file.

| What was graded | Grade | In one sentence | Detail |
|-----------------|-------|-----------------|--------|
| Stability | **B** | Failures degrade, get logged, and are caught by 71 real measurements — except in four file readers that refuse a file and say nothing about why. | `metric-stability.md` |
| Architecture | **B+** | The layering is real and re-checked on every push, and the C++/shader contract is enforced by the compiler — but adding one rendering step means editing eight files, and forgetting one fails in silence. | `metric-architecture.md` |
| Capabilities | **C** | The rendering is strong and everything present is measured; character animation, on-screen text, compressed textures and multi-core work are all absent, and saved levels exist as a library no game can reach. | `metric-capabilities.md` |
| Cross-platform readiness | **B** | Windows-only code is confined to 8 of 142 files, and the hard conversion problems are already written down correctly — but with Direct3D switched off the project configures and then fails to compile. | `metric-portability.md` |
| Developer setup | **C+** | Every documented command worked first try — but 109 of 142 files disagree with the code formatter the project ships, and a settings file claims they agree. | `metric-devex.md` |
| AI tooling | **B** | The rules are unusually accurate — eight numbers checked, eight correct — but the main instruction file promises thirteen checks and lists twelve. | `metric-ai-tooling.md` |

## What this means

**The structure is the asset, and it is measured, not asserted.** Only 8 of 142
source files touch a Windows or graphics-specific interface, and every one sits
inside the package built for it. A script proves that on every push. The payoff
is that a Linux or Mac graphics backend later means writing new code beside the
old rather than rewriting the renderer. The catch is one level up: with Direct3D
switched off the project still *configures* — all the automated check tests —
but no longer *compiles*, because three lines at the top of the main application
file name Windows-only components with no guard around them.

**Several finished features cannot be reached from a game.** The scene-file
reader — the thing that would let you save a level and load it back — works and
is measured, and has no way in. Nothing in the engine ever opens a scene file,
the function takes text rather than a filename, and no scene file exists
anywhere in the project. The sandbox builds its 64 objects in C++ instead. The
same is true of prefabs and of the packed-content archive that ships beside the
game. The self-checks pass because they call these libraries directly, which is
precisely what hides the gap: "tested" and "usable" have drifted apart.

**The code formatter is a trap for the next person.** 109 of 142 files disagree
with the formatting rules the project ships, across 2,883 separate places, and
nothing anywhere runs the formatter. Anyone who opens this repository in an
editor that formats on save rewrites three-quarters of the codebase on their
first keystroke, burying whatever they came to change. The differences are the
author's deliberate column alignment, which the chosen style undoes — so the
config contradicts the house style it claims to own.

## What games this can build today

**A first-person walk through a fixed 3D scene of under 512 objects works
today**, and is roughly what the sandbox already is: shadowed realistic
lighting, physical materials, a character who handles steps and slopes, three
cameras, sound effects, and a shipping `game.exe`. You author the level in C++,
and you have no on-screen readout of any kind.

Everything else fails on one named piece. A 200-enemy action game: no character
animation at all, plus a 256-body physics limit. A text-heavy RPG: the only
letters the engine can draw are the bitmaps hand-made for the debug readout. A
2D platformer: the same missing text, plus no compressed textures, so a single
2048-pixel image costs 16 MB of graphics memory.

## The three things worth doing next

1. **Give a game a way to load a level from a file.** The reader exists and is
   measured; it needs a path-taking entry point, a mount, a real `.solscene` in
   the content folder, and a check that loads it from disk. Pair it with
   diagnostics — today four readers refuse a file across roughly ninety
   rejection paths without logging a single word about which one fired.
2. **Text on screen.** Carried over unfixed, still marked ready to start, and
   still the single absence that blocks the most game shapes: no menu, no score,
   no dialogue, no HUD without it.
3. **Settle the formatter.** Either reshape the code to the config, or reshape
   the config to permit the alignment the code already uses — then have CI check
   it. This one gets worse with every commit and traps whoever arrives next.

## Since last time

The previous run's top action is done: the line that stopped the build
configuring on Linux is now guarded, and a new thirteenth check generalises it to
every conditionally-built package. A shipped crash now leaves a log on disk too.
Five of six grades are nonetheless *lower* — not because the engine regressed,
but because this pass found the gate-only scene loader, measured the formatter
mismatch across the whole tree, and read the bands more strictly.
