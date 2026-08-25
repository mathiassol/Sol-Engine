# Instance names (Scene #2)

Date: 24 Aug 2026  
Status: implemented

## Decision

Interned ids on the instance, strings in a table on `World`. Not `std::string`
on `Instance` (World stays a copyable blob). Renderer still never sees names.

```
NameId intern_name(World&, string_view)   // 0 = unnamed / empty
void set_instance_name(World&, index, string_view)
string_view instance_name(World&, index)
u32 find_instance(World&, string_view)    // first match, else kInvalidInstance
```

31 chars stored, null-terminated. Same string interned twice returns the same
id. Duplicate instance names: `find_instance` returns the first. Empty name
does not match unnamed instances.

Sandbox names huskies `husky_N` and the floor `ground`.

## Gate

`Scene name gate: intern=yes find=yes unnamed=yes dup=first miss=yes (pass)`

## Not this

- Hierarchy / parenting (Scene #3).
- Scene file (Scene #4).
- Inspector, unique-name enforcement, case folding.
