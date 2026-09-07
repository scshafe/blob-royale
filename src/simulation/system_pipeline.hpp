#ifndef BLOB_ROYALE_SIMULATION_SYSTEM_PIPELINE_HPP
#define BLOB_ROYALE_SIMULATION_SYSTEM_PIPELINE_HPP

#include "simulation_system.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

// canonical: system_stage -- the three named seams cut into the fixed tick kernel.
//
// Three, because the boundary that decides how many there are is what a system can *see*.
// `kPreKernel` reads start-of-tick positions and this tick's recorded commands and writes body
// intent; `kPostKernel` reads committed positions, the rebuilt spatial index, and this tick's
// world events, and writes consequences; `kLifecycle` reads the tick's final world and writes
// roster and match bookkeeping (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one
// fixed kernel, three named stages").
enum class SystemStage : std::uint8_t {
  kPreKernel = 0,
  kPostKernel = 1,
  kLifecycle = 2,
};

// The stages in kernel execution order. The pipeline groups by this order and the kernel runs it,
// so no caller maintains a second list.
inline constexpr std::array<SystemStage, 3> kSystemStages{
    SystemStage::kPreKernel, SystemStage::kPostKernel, SystemStage::kLifecycle};

inline constexpr std::size_t kSystemStageCount = kSystemStages.size();

// The enumerator value of a stage is its index in kSystemStages, which is what lets the pipeline
// index its per-stage runs by `static_cast<std::size_t>(stage)` without a lookup table.
static_assert(static_cast<std::size_t>(SystemStage::kPreKernel) == 0 &&
                  static_cast<std::size_t>(SystemStage::kPostKernel) == 1 &&
                  static_cast<std::size_t>(SystemStage::kLifecycle) == 2 && kSystemStageCount == 3,
              "every SystemStage enumerator must equal its index in kSystemStages");

// The declared name of one stage, for diagnostics and fixtures.
[[nodiscard]] constexpr std::string_view system_stage_name(const SystemStage stage) noexcept {
  switch (stage) {
  case SystemStage::kPreKernel:
    return "pre_kernel";
  case SystemStage::kPostKernel:
    return "post_kernel";
  case SystemStage::kLifecycle:
    return "lifecycle";
  }
  return "system_stage_invalid";
}

// canonical: system_pipeline -- the ordered systems of one mode, grouped by stage.
// @extension-point simulation_system
//
// The pipeline stable-partitions the declared list by stage and preserves the declared order
// inside each stage, so **precedence is a property of the mode's written list and never of
// insertion, allocation, or static-initialization order**
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages"). A duplicate system name is rejected at construction so a later failure names one
// system rather than an ambiguous pair.
//
// A system is `std::unique_ptr<const SimulationSystem>`: the rule set is immutable-after-
// construction polymorphic policy owned for the life of the match and never copied per tick, which
// is why owning pointers here do not weaken the value semantics that govern world state.
// related: simulation_system.hpp -- the interface one row holds.
// related: game_simulation.hpp -- the kernel that runs each stage in order.
class SystemPipeline final {
public:
  struct StagedSystem final {
    SystemStage stage;
    std::unique_ptr<const SimulationSystem> system;
  };

  // Stable-partitions by stage. Throws SimulationValidationError for a null system, an empty
  // name, and a duplicate name.
  [[nodiscard]] static SystemPipeline create(std::vector<StagedSystem> declared_systems);

  // The pipeline of a mode that declares no system: every stage is empty and the tick is the
  // accepted seven-phase baseline. This is the stricter of ADR 0003's two bit-identity witnesses.
  [[nodiscard]] static SystemPipeline empty();

  // This pipeline with one more system appended after everything the mode declared at that stage.
  //
  // It exists for exactly one caller: the engine appends its own MatchLifecycleSystem last at
  // kLifecycle when a simulation is constructed, and the ADR is explicit that the lifecycle system
  // is a SimulationSystem like any other rather than a second kind of tick participant
  // (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
  // Appending re-runs `create`'s validation, so a mode that declares a system named
  // `match_lifecycle` is rejected at construction instead of shadowing the engine's.
  [[nodiscard]] SystemPipeline with_appended(StagedSystem staged) &&;

  SystemPipeline(const SystemPipeline&) = delete;
  SystemPipeline(SystemPipeline&&) noexcept = default;
  SystemPipeline& operator=(const SystemPipeline&) = delete;
  SystemPipeline& operator=(SystemPipeline&&) noexcept = default;
  ~SystemPipeline() = default;

  // The stage's systems in declared order. Total: a stage no system declared returns an empty
  // span rather than failing.
  [[nodiscard]] std::span<const StagedSystem> systems_at(SystemStage stage) const& noexcept;
  [[nodiscard]] std::span<const StagedSystem> systems_at(SystemStage) const&& = delete;

  // The declared system count across every stage.
  [[nodiscard]] std::size_t size() const noexcept { return staged_systems_.size(); }

private:
  using StageOffsets = std::array<std::size_t, kSystemStageCount + 1>;

  SystemPipeline(std::vector<StagedSystem> staged_systems, StageOffsets stage_offsets) noexcept;

  // Partitioned by stage in kSystemStages order; declared order is preserved inside each run.
  std::vector<StagedSystem> staged_systems_;
  // Half-open [stage_offsets_[i], stage_offsets_[i + 1]) is the run of stage kSystemStages[i].
  StageOffsets stage_offsets_;
};

} // namespace blob_royale::simulation

#endif
