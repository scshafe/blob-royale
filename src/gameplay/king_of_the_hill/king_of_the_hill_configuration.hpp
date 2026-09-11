#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_KING_OF_THE_HILL_CONFIGURATION_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: king_of_the_hill_configuration -- the validated `[king_of_the_hill]` section, in the
// units systems read.
//
// One strict configuration section carries every balance number this mode owns, exactly as
// `royale/royale_configuration.hpp` does for royale: the application parses it and hands the
// validated value to the mode factory; the mode holds it and hands it to the systems it builds,
// which is the only way configuration reaches a tick.
//
// **Every key is required, no key has a silent default, and every numeric value must be finite**
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "King of the hill" § "Mode
// configuration"). `Section` is the authored form -- seconds, world units, a whole number of
// points, one boolean, and a closed motion policy -- and this class is what a system holds: every
// duration is already a tick count, and nothing below the factory can see a second.
//
// Balance values are configuration. Changing a number here needs no ADR amendment; changing a
// *rule* -- the hill's tour, the scoring table, the objective's order -- does.
// related: king_of_the_hill_mode.hpp -- the mode that holds one of these.
// related: ../shared/duration_ticks.hpp -- the one conversion the authored durations go through.
// related: ../gameplay_validation_error.hpp -- the `GAMEPLAY.KING_OF_THE_HILL_*` rejections.
class KingOfTheHillConfiguration final {
public:
  enum class HillMotionPolicy { kMarkerTour, kRandomRoam };

  // The proposed values of ADR 0007's table. They are the mode's shipped balance until a playtest
  // says otherwise, and they are what `GameModeRegistry::create(mode_name)` -- the defaults-only
  // overload a test or a diagnostic uses -- builds.
  static constexpr double kDefaultHillRadiusWorldUnits = 90.0;
  static constexpr double kDefaultHillDwellSeconds = 12.0;
  static constexpr double kDefaultHillTravelSeconds = 4.0;
  static constexpr double kDefaultPointIntervalSeconds = 1.0;
  static constexpr std::uint64_t kDefaultPointsToWin = 30;
  static constexpr bool kDefaultContestedHillScores = false;
  static constexpr double kDefaultTimeLimitSeconds = 240.0;
  static constexpr double kDefaultRespawnDelaySeconds = 2.0;
  static constexpr double kDefaultCountdownSeconds = 5.0;
  static constexpr double kDefaultRestartDelaySeconds = 8.0;
  static constexpr double kDefaultHillSpeedMinimum = 20.0;
  static constexpr double kDefaultHillSpeedMaximum = 70.0;
  static constexpr double kDefaultHillRetargetMinimumSeconds = 0.35;
  static constexpr double kDefaultHillRetargetMaximumSeconds = 1.2;
  // The floor preserves dominant-axis motion at the maximum map extent at the fixed clock.
  // Speed is the sampled scalar; the realized vector norm is naturally floating-point rounded.
  static constexpr double kMinimumHillSpeed = 0x1p-10;
  static constexpr double kMaximumHillSpeed = 1'000'000.0;
  static constexpr double kMaximumHillRetargetSeconds = 3'600.0;

  // The `[king_of_the_hill]` section as authored, one member per key. Units are in the names
  // because the value alone cannot carry them.
  struct Section final {
    double hill_radius_world_units;
    double hill_dwell_seconds;
    double hill_travel_seconds;
    double point_interval_seconds;
    std::uint64_t points_to_win;
    bool contested_hill_scores;
    double time_limit_seconds;
    double respawn_delay_seconds;
    double countdown_seconds;
    double restart_delay_seconds;
    HillMotionPolicy hill_motion;
    double hill_speed_minimum;
    double hill_speed_maximum;
    double hill_retarget_minimum_seconds;
    double hill_retarget_maximum_seconds;

    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates the authored section and converts its durations. Throws GameplayValidationError
  // naming the key that failed, in the `king_of_the_hill.<key>` form the section is authored in.
  //
  // The rules, checked in the section's own key order so a section with two mistakes always
  // names the same one: the hill radius finite and strictly positive; dwell, travel, the point
  // interval, the respawn delay,
  // the countdown, and the restart delay finite and not negative, through `duration_ticks`; dwell
  // plus travel converting to at least one tick, because a tour whose every stop is instantaneous
  // has no position to hold; `points_to_win` at least one; and the time limit finite and strictly
  // positive, because a match that may never end is not the game.
  // Both motion policies require finite, ordered scalar speeds in [2^-10, 1000000] wu/s and
  // positive retarget seconds at most 3600, ordered before and after conversion to positive ticks.
  [[nodiscard]] static KingOfTheHillConfiguration create(const Section& section);

  // Parses the closed authored vocabulary; unknown text raises the mode's motion-policy error.
  [[nodiscard]] static HillMotionPolicy parse_motion_policy(std::string_view value);

  // The proposed section above, already validated.
  [[nodiscard]] static KingOfTheHillConfiguration defaults();

  // The authored section every default names, for a configuration test or a generated template.
  [[nodiscard]] static Section default_section() noexcept;

  KingOfTheHillConfiguration(const KingOfTheHillConfiguration&) = default;
  KingOfTheHillConfiguration(KingOfTheHillConfiguration&&) noexcept = default;
  KingOfTheHillConfiguration& operator=(const KingOfTheHillConfiguration&) = default;
  KingOfTheHillConfiguration& operator=(KingOfTheHillConfiguration&&) noexcept = default;
  ~KingOfTheHillConfiguration() = default;

  // wu. The hill's radius in every phase.
  [[nodiscard]] double hill_radius() const noexcept { return hill_radius_; }
  // `D`: ticks the hill holds at each marker before it glides on.
  [[nodiscard]] std::uint64_t hill_dwell_ticks() const noexcept { return hill_dwell_ticks_; }
  // `T`: ticks the hill takes to glide from one marker to the next. Zero is a hop.
  [[nodiscard]] std::uint64_t hill_travel_ticks() const noexcept { return hill_travel_ticks_; }
  // `I`: consecutive inside ticks that earn one point. Zero scores on the first inside tick.
  [[nodiscard]] std::uint64_t point_interval_ticks() const noexcept {
    return point_interval_ticks_;
  }
  [[nodiscard]] std::uint64_t points_to_win() const noexcept { return points_to_win_; }
  // Whether everyone inside a hill held by more than one player scores, or nobody does.
  [[nodiscard]] bool contested_hill_scores() const noexcept { return contested_hill_scores_; }
  [[nodiscard]] std::uint64_t time_limit_ticks() const noexcept { return time_limit_ticks_; }
  // `D` of `shared/respawn_system.hpp`: ticks a knocked-out player is out of play.
  [[nodiscard]] std::uint64_t respawn_delay_ticks() const noexcept { return respawn_delay_ticks_; }
  [[nodiscard]] std::uint64_t countdown_ticks() const noexcept { return countdown_ticks_; }
  [[nodiscard]] std::uint64_t restart_delay_ticks() const noexcept { return restart_delay_ticks_; }
  [[nodiscard]] HillMotionPolicy motion_policy() const noexcept { return motion_policy_; }
  [[nodiscard]] double hill_speed_minimum() const noexcept { return hill_speed_minimum_; }
  [[nodiscard]] double hill_speed_maximum() const noexcept { return hill_speed_maximum_; }
  [[nodiscard]] std::uint64_t hill_retarget_minimum_ticks() const noexcept {
    return hill_retarget_minimum_ticks_;
  }
  [[nodiscard]] std::uint64_t hill_retarget_maximum_ticks() const noexcept {
    return hill_retarget_maximum_ticks_;
  }

  friend bool operator==(const KingOfTheHillConfiguration&,
                         const KingOfTheHillConfiguration&) = default;

private:
  KingOfTheHillConfiguration(double hill_radius, std::uint64_t hill_dwell_ticks,
                             std::uint64_t hill_travel_ticks, std::uint64_t point_interval_ticks,
                             std::uint64_t points_to_win, bool contested_hill_scores,
                             std::uint64_t time_limit_ticks, std::uint64_t respawn_delay_ticks,
                             std::uint64_t countdown_ticks, std::uint64_t restart_delay_ticks,
                             HillMotionPolicy motion_policy, double hill_speed_minimum,
                             double hill_speed_maximum, std::uint64_t hill_retarget_minimum_ticks,
                             std::uint64_t hill_retarget_maximum_ticks) noexcept;

  double hill_radius_;
  std::uint64_t hill_dwell_ticks_;
  std::uint64_t hill_travel_ticks_;
  std::uint64_t point_interval_ticks_;
  std::uint64_t points_to_win_;
  bool contested_hill_scores_;
  std::uint64_t time_limit_ticks_;
  std::uint64_t respawn_delay_ticks_;
  std::uint64_t countdown_ticks_;
  std::uint64_t restart_delay_ticks_;
  HillMotionPolicy motion_policy_;
  double hill_speed_minimum_;
  double hill_speed_maximum_;
  std::uint64_t hill_retarget_minimum_ticks_;
  std::uint64_t hill_retarget_maximum_ticks_;
};

} // namespace blob_royale::gameplay

#endif
