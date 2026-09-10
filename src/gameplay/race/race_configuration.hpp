#ifndef BLOB_ROYALE_GAMEPLAY_RACE_RACE_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_RACE_RACE_CONFIGURATION_HPP

#include <cstdint>

namespace blob_royale::gameplay {

// canonical: race_configuration -- the strict `[race]` section, validated once and converted to
// the units systems consume. Every authored key is required; `defaults` is only the explicit
// defaults-only factory path. Rules and proposed values are ADR 0007 section "Race".
// related: ../shared/duration_ticks.hpp -- the shared seconds-to-ticks conversion.
// related: race_course.hpp -- validates map geometry against these configured dimensions.
class RaceConfiguration final {
public:
  static constexpr double kDefaultThrustMaximumWorldUnitsPerSecondSquared = 400.0;
  static constexpr double kDefaultTrackHalfWidthWorldUnits = 70.0;
  static constexpr double kDefaultCheckpointRadiusWorldUnits = 40.0;
  static constexpr double kDefaultRespawnDelaySeconds = 2.0;
  static constexpr double kDefaultFinishWindowSeconds = 20.0;
  static constexpr double kDefaultTimeLimitSeconds = 240.0;
  static constexpr double kDefaultCountdownSeconds = 5.0;
  static constexpr double kDefaultRestartDelaySeconds = 8.0;

  // Authored seconds and world units, in the key order of ADR 0007's table.
  struct Section final {
    double thrust_max_world_units_per_second_squared;
    double track_half_width_world_units;
    double checkpoint_radius_world_units;
    double respawn_delay_seconds;
    double finish_window_seconds;
    double time_limit_seconds;
    double countdown_seconds;
    double restart_delay_seconds;

    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates in authored key order. Throws GameplayValidationError for invalid thrust, a
  // nonfinite/nonpositive width, radius or time limit, a radius above the half-width, or a
  // negative/nonfinite/unrepresentable duration. Race scalar errors name `race.<key>`.
  [[nodiscard]] static RaceConfiguration create(const Section& section);
  [[nodiscard]] static RaceConfiguration defaults();
  [[nodiscard]] static Section default_section() noexcept;

  RaceConfiguration(const RaceConfiguration&) = default;
  RaceConfiguration(RaceConfiguration&&) noexcept = default;
  RaceConfiguration& operator=(const RaceConfiguration&) = default;
  RaceConfiguration& operator=(RaceConfiguration&&) noexcept = default;
  ~RaceConfiguration() = default;

  [[nodiscard]] double thrust_maximum() const noexcept { return thrust_maximum_; }
  [[nodiscard]] double track_half_width() const noexcept { return track_half_width_; }
  [[nodiscard]] double checkpoint_radius() const noexcept { return checkpoint_radius_; }
  [[nodiscard]] std::uint64_t respawn_delay_ticks() const noexcept { return respawn_delay_ticks_; }
  [[nodiscard]] std::uint64_t finish_window_ticks() const noexcept { return finish_window_ticks_; }
  [[nodiscard]] std::uint64_t time_limit_ticks() const noexcept { return time_limit_ticks_; }
  [[nodiscard]] std::uint64_t countdown_ticks() const noexcept { return countdown_ticks_; }
  [[nodiscard]] std::uint64_t restart_delay_ticks() const noexcept { return restart_delay_ticks_; }

  friend bool operator==(const RaceConfiguration&, const RaceConfiguration&) = default;

private:
  RaceConfiguration(double thrust_maximum, double track_half_width, double checkpoint_radius,
                    std::uint64_t respawn_delay_ticks, std::uint64_t finish_window_ticks,
                    std::uint64_t time_limit_ticks, std::uint64_t countdown_ticks,
                    std::uint64_t restart_delay_ticks) noexcept;

  double thrust_maximum_;
  double track_half_width_;
  double checkpoint_radius_;
  std::uint64_t respawn_delay_ticks_;
  std::uint64_t finish_window_ticks_;
  std::uint64_t time_limit_ticks_;
  std::uint64_t countdown_ticks_;
  std::uint64_t restart_delay_ticks_;
};

} // namespace blob_royale::gameplay

#endif
