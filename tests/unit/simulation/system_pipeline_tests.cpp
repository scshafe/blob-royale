#include "simulation_system.hpp"
#include "simulation_test_fixture.hpp"
#include "simulation_validation_error.hpp"
#include "system_pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

[[nodiscard]] std::vector<std::string_view>
declared_names(const simulation::SystemPipeline& pipeline, const simulation::SystemStage stage) {
  std::vector<std::string_view> names;
  for (const simulation::SystemPipeline::StagedSystem& staged : pipeline.systems_at(stage)) {
    names.push_back(staged.system->name());
  }
  return names;
}

[[nodiscard]] simulation::SimulationValidationCode
rejection_code_of(std::vector<simulation::SystemPipeline::StagedSystem> declared_systems) {
  try {
    const simulation::SystemPipeline rejected =
        simulation::SystemPipeline::create(std::move(declared_systems));
  } catch (const simulation::SimulationValidationError& failure) {
    return failure.validation_code();
  }
  FAIL("SystemPipeline::create accepted a declared list it must reject");
  return simulation::SimulationValidationCode::kSystemPipelineStageUnknown;
}

} // namespace

TEST_CASE("SystemPipeline stable-partitions the declared list into the three named stages",
          "[unit][simulation][system_pipeline]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kLifecycle, "lifecycle_first"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, "pre_first"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPostKernel, "post_first"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, "pre_second"));

  const simulation::SystemPipeline pipeline =
      simulation::SystemPipeline::create(std::move(declared));

  CHECK(pipeline.size() == 4);
  CHECK(declared_names(pipeline, simulation::SystemStage::kPreKernel) ==
        std::vector<std::string_view>{"pre_first", "pre_second"});
  CHECK(declared_names(pipeline, simulation::SystemStage::kPostKernel) ==
        std::vector<std::string_view>{"post_first"});
  CHECK(declared_names(pipeline, simulation::SystemStage::kLifecycle) ==
        std::vector<std::string_view>{"lifecycle_first"});
}

TEST_CASE("SystemPipeline preserves the declared order inside one stage",
          "[unit][simulation][system_pipeline][order]") {
  // Precedence is a property of the mode's written list and never of insertion, allocation, or
  // static-initialization order, so the declared order survives regardless of how the stages
  // interleave in the declaration.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPostKernel, "post_alpha"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPostKernel, "post_beta"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, "pre_alpha"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPostKernel, "post_gamma"));

  const simulation::SystemPipeline pipeline =
      simulation::SystemPipeline::create(std::move(declared));

  CHECK(declared_names(pipeline, simulation::SystemStage::kPostKernel) ==
        std::vector<std::string_view>{"post_alpha", "post_beta", "post_gamma"});
}

TEST_CASE("SystemPipeline rejects a duplicate system name",
          "[unit][simulation][system_pipeline][validation]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, "zone_shrink"));
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kLifecycle, "zone_shrink"));

  CHECK(rejection_code_of(std::move(declared)) ==
        simulation::SimulationValidationCode::kSystemPipelineDuplicateSystemName);
}

TEST_CASE("SystemPipeline rejects a declared row that holds no system",
          "[unit][simulation][system_pipeline][validation]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged(simulation::SystemStage::kPreKernel, nullptr));

  CHECK(rejection_code_of(std::move(declared)) ==
        simulation::SimulationValidationCode::kSystemPipelineSystemMissing);
}

TEST_CASE("SystemPipeline rejects a system whose name is empty",
          "[unit][simulation][system_pipeline][validation]") {
  // A name is a stable, unique, snake_case identity used by the pipeline, diagnostics, and
  // fixtures. An empty one would make duplicate detection meaningless and every later failure
  // anonymous.
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, ""));

  CHECK(rejection_code_of(std::move(declared)) ==
        simulation::SimulationValidationCode::kSystemPipelineSystemNameEmpty);
}

TEST_CASE("SystemPipeline empty declares no system at any stage",
          "[unit][simulation][system_pipeline]") {
  const simulation::SystemPipeline pipeline = simulation::SystemPipeline::empty();

  CHECK(pipeline.size() == 0);
  for (const simulation::SystemStage stage : simulation::kSystemStages) {
    INFO("stage " << simulation::system_stage_name(stage));
    CHECK(pipeline.systems_at(stage).empty());
  }
}

TEST_CASE("SystemPipeline systems_at is total for a stage no system declared",
          "[unit][simulation][system_pipeline]") {
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPostKernel, "only_post"));

  const simulation::SystemPipeline pipeline =
      simulation::SystemPipeline::create(std::move(declared));

  CHECK(pipeline.systems_at(simulation::SystemStage::kPreKernel).empty());
  CHECK(pipeline.systems_at(simulation::SystemStage::kLifecycle).empty());
  CHECK(pipeline.systems_at(simulation::SystemStage::kPostKernel).size() == 1);
}

TEST_CASE("SystemPipeline is a move-only owner of immutable policy",
          "[unit][simulation][system_pipeline]") {
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<simulation::SystemPipeline>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<simulation::SystemPipeline>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::SystemPipeline>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<simulation::SystemPipeline>);
}

TEST_CASE("SystemStage names every stage in kernel execution order",
          "[unit][simulation][system_pipeline]") {
  STATIC_REQUIRE(simulation::kSystemStageCount == 3);
  CHECK(simulation::kSystemStages[0] == simulation::SystemStage::kPreKernel);
  CHECK(simulation::kSystemStages[1] == simulation::SystemStage::kPostKernel);
  CHECK(simulation::kSystemStages[2] == simulation::SystemStage::kLifecycle);
  CHECK(simulation::system_stage_name(simulation::SystemStage::kPreKernel) == "pre_kernel");
  CHECK(simulation::system_stage_name(simulation::SystemStage::kPostKernel) == "post_kernel");
  CHECK(simulation::system_stage_name(simulation::SystemStage::kLifecycle) == "lifecycle");
}

TEST_CASE("systems_at rejects a stage outside the closed enumeration rather than reading past it",
          "[unit][simulation][system_pipeline][validation]") {
  // `create` already rejects a declared row whose stage is outside kSystemStages; `systems_at`
  // indexed the same four-element offsets array with an unchecked cast inside a `noexcept`
  // function, so the same hazard now gets the same answer (engine review finding 15).
  std::vector<simulation::SystemPipeline::StagedSystem> declared;
  declared.push_back(testing::staged_no_op(simulation::SystemStage::kPreKernel, "first"));
  const simulation::SystemPipeline pipeline =
      simulation::SystemPipeline::create(std::move(declared));

  try {
    static_cast<void>(pipeline.systems_at(static_cast<simulation::SystemStage>(7)));
    FAIL("systems_at accepted a stage outside the closed enumeration");
  } catch (const simulation::SimulationValidationError& failure) {
    CHECK(failure.validation_code() ==
          simulation::SimulationValidationCode::kSystemPipelineStageUnknown);
    CHECK(failure.code() == std::string_view{"SIMULATION.SYSTEM_PIPELINE_STAGE_UNKNOWN"});
    CHECK(failure.context() == "system_pipeline.systems_at.stage");
  }

  // Every declared stage is still total: a stage no system declared is an empty span, not a
  // failure.
  CHECK(pipeline.systems_at(simulation::SystemStage::kPreKernel).size() == 1);
  CHECK(pipeline.systems_at(simulation::SystemStage::kPostKernel).empty());
  CHECK(pipeline.systems_at(simulation::SystemStage::kLifecycle).empty());
  const simulation::SystemPipeline bare = simulation::SystemPipeline::empty();
  CHECK(bare.systems_at(simulation::SystemStage::kPreKernel).empty());
}

TEST_CASE("An empty pipeline rejects an unknown stage exactly as a populated one does",
          "[unit][simulation][system_pipeline][validation]") {
  const simulation::SystemPipeline pipeline = simulation::SystemPipeline::empty();

  CHECK_THROWS_AS(static_cast<void>(pipeline.systems_at(static_cast<simulation::SystemStage>(200))),
                  simulation::SimulationValidationError);
}
