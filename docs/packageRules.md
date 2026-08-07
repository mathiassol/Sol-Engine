# Package Rules

Every package in this engine must follow these rules. Review this before adding or changing a package.

- Every package has exactly one responsibility.
- Packages expose the smallest possible public API.
- Dependencies always point downward.
- No circular dependencies.
- Platform-specific code lives only inside platform implementations.
- Renderer never includes graphics API headers.
- Public headers never expose implementation details.
- All packages build independently.

## Interface / implementation split

When a system has swappable backends, split it into two packages:

| Package | Contains |
|---------|----------|
| `foo` | Interfaces and data types only (`IFoo`, descriptors, handles) |
| `foo-bar` | One concrete implementation of `IFoo` |

Examples: `platform` / `platform-win32`, `rhi` / `rhi-d3d12`, `assets` / `assets-filesystem`.

The interface package must not depend on its implementations. Applications (e.g. `sandbox`) link the implementation they want.

## Public API checklist

- One factory function or entry header per implementation package (e.g. `create_platform()`).
- No platform or graphics API headers in public headers above the implementation layer.
- Prefer forward declarations; include only what the header needs.
- Implementation details live in `src/`, never in `include/`.
