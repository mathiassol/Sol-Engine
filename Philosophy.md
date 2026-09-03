# Engine Development Philosophy

*How to build. What we are building toward is [VISION.md](VISION.md).*

## General

-   Build architecture before features.
-   Keep every system focused.
-   Prefer simplicity over cleverness.
-   Optimize only after measuring.
-   Design for change, not today.
-   Every abstraction must earn its place.
-   Remove duplication early.
-   Make debugging a first-class feature.
-   Consistency beats perfection.
-   Keep APIs boring and predictable.

## Dependencies

-   Dependencies only point downward.
-   Avoid circular dependencies.
-   High-level systems depend on low-level systems.
-   Never let low-level systems know about gameplay.
-   Keep packages reusable.

## Code

-   Prefer composition over inheritance.
-   Prefer data over objects.
-   Keep ownership obvious.
-   Avoid hidden global state.
-   Pass context explicitly.
-   Write deterministic code where possible.
-   RAII everywhere.
-   Minimize dynamic allocation.
-   Favor immutable data when practical.

## Renderer

-   Think in render passes.
-   Build around a render graph.
-   Separate RHI from renderer.
-   Never expose API-specific code above the RHI.
-   Every pass has clear inputs and outputs.
-   Avoid pass side effects.
-   Treat GPU memory as a managed resource.
-   Design for multiple viewports.
-   Design for multiple frames in flight.

## GPU

-   Think in data movement.
-   Minimize synchronization.
-   Batch work whenever possible.
-   Prefer coherent memory access.
-   Design for parallelism.
-   Profile CPU and GPU independently.
-   Assume bandwidth is limited.

## Assets

-   Separate asset data from GPU resources.
-   Support hot reloading.
-   Cache aggressively.
-   Stream when possible.
-   Version serialized data.

## Editor

-   The engine and the editor are separate products.
-   Games (including the sandbox demo) are written in code against engine APIs.
-   An editor, when it exists, is another app that only exposes what the engine already can do.
-   Do not put an inspector, hierarchy, or content browser inside the engine or sandbox.
-   Debug visualization (F3 / F4 / F5, hot-reload) is an engine feature, not an editor.

## Performance

-   Measure before optimizing.
-   Eliminate unnecessary work.
-   Cache expensive calculations.
-   Scale quality independently.
-   Favor stable frame times.
-   Avoid premature optimization.

## Cross Platform

-   Isolate platform code.
-   Never mix platform code with engine logic.
-   Keep platform APIs behind interfaces.
-   Test on multiple platforms early.

## Architecture

-   One system, one responsibility.
-   Small modules age well.
-   Prefer explicit data flow.
-   Make invalid states impossible.
-   Design for testing.
-   Build for maintainability.
-   Assume the project will double in size.
-   Future you is another developer.
