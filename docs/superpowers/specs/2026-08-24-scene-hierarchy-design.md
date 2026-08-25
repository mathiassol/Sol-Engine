# Hierarchy / parenting (Scene #3)

Date: 24 Aug 2026  
Status: implemented

## Decision

`Instance.model` is **local**. `parent` is `kInvalidInstance` when unparented.
World matrix walks parents: `parent_world * local` (column-major, child in
parent space). Extract copies `instance_world_model`, not the local matrix.
Renderer still does not include `scene`.

```
bool set_instance_parent(World&, index, parent, keep_world = true)
u32 instance_parent(World&, index)
Mat4 instance_world_model(World&, index)
```

`keep_world` retargets local via `inverse_affine(parent_world) * child_world`
so the instance does not jump. Unparent with `keep_world` bakes world into
local. Self-parent and cycles return false and leave the tree unchanged.

64 instances: walk each extract, no dirty flags.

Sandbox: `husky_1` is parented to `husky_0` (keep world). Z/X nudges the
parent; the child follows.

## Gate

`Scene hierarchy gate: compose=yes cycle=no keep_world=yes unparent=yes (pass)`

1. Parent T(10,0,0), child local T(1,0,0), grandchild local T(0,2,0) →
   child world (11,0,0), grandchild (11,2,0). `keep_world=false`.
2. Cycle and self-parent rejected.
3. Reparent with `keep_world` leaves world origin unchanged.
4. Unparent with `keep_world` keeps the composed origin.

## Not this

- Scene file (Scene #4, now Ready).
- Scale/shear-correct inverse (affine orthonormal inverse is enough).
- Reparent UI / inspector.
