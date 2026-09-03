# Project Scaffold

*The founding statement, kept as written. For what the engine is being built
toward now, see [VISION.md](VISION.md) — this file is history and is not
updated.*

The project starts as a collection of small, independent C++ packages rather than a single engine project. Each package has one clear responsibility and communicates through clean, well-defined interfaces. Dependencies always point downward, and packages should be reusable outside of the engine whenever possible. The goal is to build a renderer and engine architecture that can grow for years without requiring major rewrites. Systems should be designed around extensibility, explicit ownership, and maintainability rather than short-term convenience.

Development targets Windows as the primary platform, while the architecture is designed from day one to support macOS and mainstream Linux distributions without platform-specific assumptions leaking into higher-level code. Platform functionality, window creation, graphics backends, input, and filesystem interactions should all be isolated behind dedicated packages. The renderer should be built on top of a platform-independent rendering abstraction, allowing different graphics APIs to be implemented as interchangeable backends. Features are added only after the underlying architecture is capable of supporting them cleanly, ensuring every new system integrates naturally into the existing framework rather than becoming another special case.
