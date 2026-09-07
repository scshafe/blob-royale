#include "system_pipeline.hpp"

#include "simulation_validation_error.hpp"

#include <string>
#include <utility>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] std::string declared_position(const std::size_t declared_index) {
  return " (declared system " + std::to_string(declared_index) + ")";
}

void validate_declared_system(const SystemPipeline::StagedSystem& staged,
                              const std::size_t declared_index) {
  if (staged.system == nullptr) {
    throw SimulationValidationError(
        SimulationValidationCode::kSystemPipelineSystemMissing, "system_pipeline.systems",
        "a declared stage row holds no system" + declared_position(declared_index));
  }
  if (staged.system->name().empty()) {
    throw SimulationValidationError(
        SimulationValidationCode::kSystemPipelineSystemNameEmpty, "system_pipeline.systems.name",
        "a system name must be a stable, unique, snake_case identity and must not be empty" +
            declared_position(declared_index));
  }
}

// Quadratic over the declared list on purpose: a mode declares a handful of systems, and scanning
// in declared order names the *first* declared row a duplicate collides with, which is the row an
// author has to edit. A sort would report an arbitrary member of the colliding pair.
void validate_unique_names(const std::vector<SystemPipeline::StagedSystem>& declared_systems) {
  for (std::size_t index = 1; index < declared_systems.size(); ++index) {
    const std::string_view name = declared_systems[index].system->name();
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (declared_systems[earlier].system->name() != name) {
        continue;
      }
      throw SimulationValidationError(SimulationValidationCode::kSystemPipelineDuplicateSystemName,
                                      "system_pipeline.systems.name",
                                      "system name " + std::string(name) +
                                          " is already declared by system " +
                                          std::to_string(earlier) + declared_position(index));
    }
  }
}

} // namespace

SystemPipeline SystemPipeline::create(std::vector<StagedSystem> declared_systems) {
  for (std::size_t index = 0; index < declared_systems.size(); ++index) {
    validate_declared_system(declared_systems[index], index);
  }
  validate_unique_names(declared_systems);

  // The stable partition, written as one pass per stage in kernel execution order. Appending in
  // declared order inside each pass is what makes precedence a property of the mode's written list
  // rather than of the sort algorithm's stability guarantee.
  std::vector<StagedSystem> staged_systems;
  staged_systems.reserve(declared_systems.size());
  StageOffsets stage_offsets{};
  for (std::size_t stage_index = 0; stage_index < kSystemStageCount; ++stage_index) {
    stage_offsets[stage_index] = staged_systems.size();
    for (StagedSystem& declared : declared_systems) {
      if (declared.system != nullptr && declared.stage == kSystemStages[stage_index]) {
        staged_systems.push_back(std::move(declared));
      }
    }
  }
  stage_offsets[kSystemStageCount] = staged_systems.size();

  // Every declared row landed in exactly one stage run. A row whose stage is outside the closed
  // enumeration would be silently dropped otherwise, which is the one way this partition could
  // lose a mode's rule without saying so.
  if (staged_systems.size() != declared_systems.size()) {
    throw SimulationValidationError(
        SimulationValidationCode::kSystemPipelineStageUnknown, "system_pipeline.systems.stage",
        "a declared system names a SystemStage outside the closed enumeration");
  }

  return SystemPipeline(std::move(staged_systems), stage_offsets);
}

SystemPipeline SystemPipeline::empty() { return SystemPipeline({}, StageOffsets{}); }

SystemPipeline SystemPipeline::with_appended(StagedSystem staged) && {
  // The partitioned vector is already in kernel stage order with declared order preserved inside
  // each run, so re-declaring from it and appending the new row reproduces the same partition with
  // the row last within its stage.
  std::vector<StagedSystem> declared_systems = std::move(staged_systems_);
  declared_systems.push_back(std::move(staged));
  return create(std::move(declared_systems));
}

std::span<const SystemPipeline::StagedSystem>
SystemPipeline::systems_at(const SystemStage stage) const& noexcept {
  const auto stage_index = static_cast<std::size_t>(stage);
  const std::size_t first = stage_offsets_[stage_index];
  const std::size_t last = stage_offsets_[stage_index + 1];
  return std::span<const StagedSystem>(staged_systems_).subspan(first, last - first);
}

SystemPipeline::SystemPipeline(std::vector<StagedSystem> staged_systems,
                               const StageOffsets stage_offsets) noexcept
    : staged_systems_(std::move(staged_systems)), stage_offsets_(stage_offsets) {}

} // namespace blob_royale::simulation
