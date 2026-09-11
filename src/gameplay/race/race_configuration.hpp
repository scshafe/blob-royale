#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_CONFIGURATION_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: race_configuration -- the strict `[race]` section, validated once and converted to
// the units systems consume. Every authored key is required; `defaults` is only the explicit
// defaults-only factory path. Rules and proposed values are ADR 0007 section "Race".
// related: ../shared/duration_ticks.hpp -- the shared seconds-to-ticks conversion.
// related: race_course.hpp -- binds the named terrain corridor and validates the gate radius.
class RaceConfiguration final {
public:
  static constexpr std::string_view kDefaultRoad = "road";
  static constexpr double kDefaultCheckpointRadiusWorldUnits = 40.0;
  static constexpr double kDefaultRespawnDelaySeconds = 2.0;
  static constexpr double kDefaultFinishWindowSeconds = 20.0;
  static constexpr double kDefaultTimeLimitSeconds = 240.0;
  static constexpr double kDefaultCountdownSeconds = 5.0;
  static constexpr double kDefaultRestartDelaySeconds = 8.0;

  // Authored identity, seconds and world units, in the declared key order.
  struct Section final {
    std::string road;
    double checkpoint_radius_world_units;
    double respawn_delay_seconds;
    double finish_window_seconds;
    double time_limit_seconds;
    double countdown_seconds;
    double restart_delay_seconds;

    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates in authored key order. Throws GameplayValidationError for
  // RACE_ROAD_NAME_INVALID for a road outside the shared snake-case grammar/name limit, a
  // nonfinite/nonpositive radius or time limit, or a negative/nonfinite/unrepresentable duration.
  // Race errors name `race.<key>`. Corridor existence and radius-versus-width require the map
  // and are validated only by RaceCourse; this value does not duplicate terrain dimensions.
  [[nodiscard]] static RaceConfiguration create(const Section& section);
  [[nodiscard]] static RaceConfiguration defaults();
  [[nodiscard]] static Section default_section();

  RaceConfiguration(const RaceConfiguration&) = default;
  RaceConfiguration(RaceConfiguration&&) noexcept = default;
  RaceConfiguration& operator=(const RaceConfiguration&) = default;
  RaceConfiguration& operator=(RaceConfiguration&&) noexcept = default;
  ~RaceConfiguration() = default;

  [[nodiscard]] std::string_view road() const& noexcept { return road_; }
  [[nodiscard]] std::string_view road() const&& = delete;
  [[nodiscard]] double checkpoint_radius() const noexcept { return checkpoint_radius_; }
  [[nodiscard]] std::uint64_t respawn_delay_ticks() const noexcept { return respawn_delay_ticks_; }
  [[nodiscard]] std::uint64_t finish_window_ticks() const noexcept { return finish_window_ticks_; }
  [[nodiscard]] std::uint64_t time_limit_ticks() const noexcept { return time_limit_ticks_; }
  [[nodiscard]] std::uint64_t countdown_ticks() const noexcept { return countdown_ticks_; }
  [[nodiscard]] std::uint64_t restart_delay_ticks() const noexcept { return restart_delay_ticks_; }

  friend bool operator==(const RaceConfiguration&, const RaceConfiguration&) = default;

private:
  RaceConfiguration(std::string road, double checkpoint_radius, std::uint64_t respawn_delay_ticks,
                    std::uint64_t finish_window_ticks, std::uint64_t time_limit_ticks,
                    std::uint64_t countdown_ticks, std::uint64_t restart_delay_ticks);

  std::string road_;
  double checkpoint_radius_;
  std::uint64_t respawn_delay_ticks_;
  std::uint64_t finish_window_ticks_;
  std::uint64_t time_limit_ticks_;
  std::uint64_t countdown_ticks_;
  std::uint64_t restart_delay_ticks_;
};

} // namespace blob_royale::gameplay

#endif
