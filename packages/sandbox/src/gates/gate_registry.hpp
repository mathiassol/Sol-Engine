#pragma once

// Every gate, classified by whether it needs a GPU.
//
// This is data, not two hand-maintained call sequences. `--gates` runs the whole
// list through main.cpp; a headless run takes the `Cpu` entries and nothing
// else. One table means the two cannot drift, and the `gate-registry` invariant
// fails the build if a gate is defined and never classified — the same guarantee
// `static_assert(sizeof(FramePipelines) == …)` gives the pipeline table, in the
// one place a static_assert cannot reach.
//
// `Cpu` means exactly one thing: runnable with the context below and no
// rhi::IDevice. When in doubt the answer is `Gpu` — a misclassified Cpu gate
// dies loudly on a null pointer, where the reverse is a gate that quietly stops
// running anywhere.

#include "gates.hpp"

namespace sandbox {

// What a headless run can supply. No device, no shader compiler, no window, and
// no ForwardDemo — those are what `Gpu` means the absence of.
struct CpuGateContext {
    engine::platform::IFileSystem* fs = nullptr;
    engine::assets::IAssetLoader* loader = nullptr;
    engine::physics::IPhysics* physics = nullptr;
    engine::ContentLayout layout{};
    std::string scratch_dir;
};

enum class GateKind : engine::u8 { Cpu, Gpu };

struct GateEntry {
    const char* name;
    GateKind kind;
    // Null for every Gpu entry. main.cpp calls those directly, with the
    // arguments only a live device and a built demo can provide.
    bool (*cpu_fn)(const CpuGateContext&);
};

extern const GateEntry kGates[];
extern const engine::usize kGateCount;

// Runs every Cpu entry in registry order and returns whether all passed. Logs
// the count it ran: a headless run that quietly executes nothing would otherwise
// look identical to one that passed.
bool run_cpu_gates(const CpuGateContext& ctx);

} // namespace sandbox
