#ifndef BLOB_ROYALE_GAMEPLAY_RACE_COURSE_PUBLISHER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_COURSE_PUBLISHER_SYSTEM_HPP

#include "race/race_configuration.hpp"
#include "race/race_course.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: course_publisher -- stamps the race's declared course and clocks onto every frame.
// @extension-point simulation_system
//
// Last among the mode's kLifecycle systems, in every phase. Owns the declared block members and
// preserves standings. Every committed race tick therefore publishes complete course geometry,
// even after match_reset removes all participants. Rules read their constructor values, not this
// published mirror.
// related: race_mode_state.hpp -- installs the block without replacing another writer's members.
class CoursePublisherSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "course_publisher";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RaceCourse course, RaceConfiguration configuration);
  CoursePublisherSystem(RaceCourse course, RaceConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  const RaceCourse course_;
  const RaceConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
