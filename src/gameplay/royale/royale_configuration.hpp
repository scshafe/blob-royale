#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ROYALE_CONFIGURATION_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: royale_configuration -- the validated `[royale]` section, in the units systems read.
//
// One strict configuration section carries every balance number this mode owns. The application
// parses it and hands the validated value to the mode factory; the mode holds it and
// hands it to the systems it builds, which is the only way configuration reaches a tick: a system's
// members are immutable configuration and `TickContext` is mode-agnostic
// (`docs/architecture/0004-gameplay-architecture.md` § "The tick: one fixed kernel, three named
// stages").
//
// **Every key is required, no key has a silent default, and every value must be finite**
// (`docs/architecture/0005-royale-mode.md` § "Mode configuration"). `Section` is the authored form
// -- seconds and world units, one member per INI key, named so a misread unit is a compile error
// rather than a silent balance bug -- and this class is what a system holds: the two seconds-valued
// grace and shrink durations and the two engine-timed phase lengths are already tick counts, and
// nothing below the factory can see a second.
//
// Balance values are configuration. Changing a number here needs no ADR amendment; changing a
// *rule* -- the drag law, the zone shape, the elimination predicate, the objective, or the spawn
// policy -- does.
// related: royale_mode.hpp -- the mode that holds one of these.
// related: shared/duration_ticks.hpp -- the one conversion the four durations go through, which
// moved out of this file the day a second customer appeared, exactly as its note said it would.
// related: gameplay_validation_error.hpp -- the `GAMEPLAY.ROYALE_*` and `GAMEPLAY.DURATION_*`
// rejections.
class RoyaleConfiguration final {
public:
  // The proposed values of `docs/architecture/0005-royale-mode.md` § "Mode configuration". They are
  // the mode's shipped balance until a playtest says otherwise, and they are what
  // `GameModeRegistry::create(mode_name)` -- the defaults-only overload a test or a diagnostic uses
  // -- builds. A configured process reaches this mode through the `[royale]` section instead.
  static constexpr double kDefaultThrustMaximumWorldUnitsPerSecondSquared = 400.0;
  static constexpr double kDefaultZoneMinimumRadiusWorldUnits = 60.0;
  static constexpr double kDefaultZoneShrinkSeconds = 90.0;
  static constexpr double kDefaultEliminationGraceSeconds = 3.0;
  static constexpr double kDefaultCountdownSeconds = 5.0;
  static constexpr double kDefaultRestartDelaySeconds = 8.0;

  // The `[royale]` section as authored, one member per key. Units are in the names because the
  // value alone cannot carry them.
  struct Section final {
    double thrust_max_world_units_per_second_squared;
    double zone_minimum_radius_world_units;
    double zone_shrink_seconds;
    double elimination_grace_seconds;
    double countdown_seconds;
    double restart_delay_seconds;

    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates the authored section and converts its durations. Throws GameplayValidationError
  // naming the key that failed. The thrust maximum is validated through the steering system's own
  // named rule rather than a second copy of it here, so a mode cannot declare a thrust maximum the
  // system it builds would refuse.
  [[nodiscard]] static RoyaleConfiguration create(const Section& section);

  // The proposed section above, already validated.
  [[nodiscard]] static RoyaleConfiguration defaults();

  // The authored section every default names, for a configuration test or a generated template.
  [[nodiscard]] static Section default_section() noexcept;

  RoyaleConfiguration(const RoyaleConfiguration&) = default;
  RoyaleConfiguration(RoyaleConfiguration&&) noexcept = default;
  RoyaleConfiguration& operator=(const RoyaleConfiguration&) = default;
  RoyaleConfiguration& operator=(RoyaleConfiguration&&) noexcept = default;
  ~RoyaleConfiguration() = default;

  // wu/s^2. The scale `thrust_steering` applies to a validated thrust direction.
  [[nodiscard]] double thrust_maximum() const noexcept { return thrust_maximum_; }
  // wu. `R_min`, the radius the zone holds once it has finished shrinking.
  [[nodiscard]] double zone_minimum_radius() const noexcept { return zone_minimum_radius_; }
  // `T`. Zero means the zone is at `R_min` from the first evaluated tick.
  [[nodiscard]] std::uint64_t zone_shrink_ticks() const noexcept { return zone_shrink_ticks_; }
  // `G`. Zero eliminates on the first tick a centre is outside, because the increment precedes the
  // test.
  [[nodiscard]] std::uint64_t elimination_grace_ticks() const noexcept {
    return elimination_grace_ticks_;
  }
  [[nodiscard]] std::uint64_t countdown_ticks() const noexcept { return countdown_ticks_; }
  [[nodiscard]] std::uint64_t restart_delay_ticks() const noexcept { return restart_delay_ticks_; }

  friend bool operator==(const RoyaleConfiguration&, const RoyaleConfiguration&) = default;

private:
  RoyaleConfiguration(double thrust_maximum, double zone_minimum_radius,
                      std::uint64_t zone_shrink_ticks, std::uint64_t elimination_grace_ticks,
                      std::uint64_t countdown_ticks, std::uint64_t restart_delay_ticks) noexcept;

  double thrust_maximum_;
  double zone_minimum_radius_;
  std::uint64_t zone_shrink_ticks_;
  std::uint64_t elimination_grace_ticks_;
  std::uint64_t countdown_ticks_;
  std::uint64_t restart_delay_ticks_;
};

} // namespace blob_royale::gameplay

#endif
