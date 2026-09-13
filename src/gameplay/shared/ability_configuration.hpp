#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_ABILITY_CONFIGURATION_HPP

#include <cstdint>

namespace blob_royale::gameplay {

// canonical: ability_configuration -- validated authored ability durations and charge bounds.
// Durations convert once at load into exact tick counts. Shield cooldown may round to zero;
// all effect durations and charge cooldown must remain positive. Charge active time is independent
// of cooldown. The charge gain is dimensionless and bounded against the greatest live movement
// ceiling by the separately validated safety envelope.
// related: shared/ability_system.hpp -- activation captures effect durations from this value.
class AbilityConfiguration final {
public:
  static constexpr double kDefaultShieldDurationSeconds = 0.4;
  static constexpr double kDefaultShieldPerfectWindowSeconds = 0.08;
  static constexpr double kDefaultShieldCooldownSeconds = 0.9;
  static constexpr double kDefaultParryStunDurationSeconds = 0.6;
  static constexpr double kDefaultChargeCooldownSeconds = 1.2;
  static constexpr double kDefaultChargeSpeedFraction = 0.75;
  static constexpr double kDefaultChargeSafetyEnvelopeSpeed = 20'000.0;
  static constexpr double kDefaultChargeActiveDurationSeconds = 0.5;
  static constexpr double kDefaultChargeHitStunDurationSeconds = 0.6;

  // Throws named GAMEPLAY.DURATION_* / ABILITY_* validation errors, with abilities.<key>
  // context, for nonfinite, negative, rounded-away, unrepresentable or inconsistent values.
  // Trailing defaults support programmatic fixtures; the authored loader requires every key.
  [[nodiscard]] static AbilityConfiguration
  create(double shield_duration_seconds, double shield_perfect_window_seconds,
         double shield_cooldown_seconds, double parry_stun_duration_seconds,
         double charge_cooldown_seconds, double charge_speed_fraction,
         double charge_safety_envelope_speed,
         double charge_active_duration_seconds = kDefaultChargeActiveDurationSeconds,
         double charge_hit_stun_duration_seconds = kDefaultChargeHitStunDurationSeconds);

  // Initial authored tuning, including a 200-tick charge attempt and 240-tick hit stun at 400 Hz.
  [[nodiscard]] static AbilityConfiguration defaults();

  [[nodiscard]] std::uint64_t shield_duration_ticks() const noexcept {
    return shield_duration_ticks_;
  }
  [[nodiscard]] std::uint64_t shield_perfect_window_ticks() const noexcept {
    return shield_perfect_window_ticks_;
  }
  [[nodiscard]] std::uint64_t shield_cooldown_ticks() const noexcept {
    return shield_cooldown_ticks_;
  }
  [[nodiscard]] std::uint64_t parry_stun_duration_ticks() const noexcept {
    return parry_stun_duration_ticks_;
  }
  [[nodiscard]] std::uint64_t charge_cooldown_ticks() const noexcept {
    return charge_cooldown_ticks_;
  }
  [[nodiscard]] std::uint64_t charge_active_duration_ticks() const noexcept {
    return charge_active_duration_ticks_;
  }
  [[nodiscard]] std::uint64_t charge_hit_stun_duration_ticks() const noexcept {
    return charge_hit_stun_duration_ticks_;
  }
  // Initial dimensionless gain, copied into live match tuning at startup. Ability activation reads
  // the current match value, so the room slider changes future bursts without rewriting this value.
  [[nodiscard]] double charge_speed_fraction() const noexcept { return charge_speed_fraction_; }
  // World units per second, the greatest post-burst speed the system will commit.
  [[nodiscard]] double charge_safety_envelope_speed() const noexcept {
    return charge_safety_envelope_speed_;
  }

  friend bool operator==(const AbilityConfiguration&, const AbilityConfiguration&) = default;

private:
  AbilityConfiguration(std::uint64_t shield_duration_ticks,
                       std::uint64_t shield_perfect_window_ticks,
                       std::uint64_t shield_cooldown_ticks, std::uint64_t parry_stun_duration_ticks,
                       std::uint64_t charge_cooldown_ticks, double charge_speed_fraction,
                       double charge_safety_envelope_speed,
                       std::uint64_t charge_active_duration_ticks,
                       std::uint64_t charge_hit_stun_duration_ticks) noexcept;

  std::uint64_t shield_duration_ticks_;
  std::uint64_t shield_perfect_window_ticks_;
  std::uint64_t shield_cooldown_ticks_;
  std::uint64_t parry_stun_duration_ticks_;
  std::uint64_t charge_cooldown_ticks_;
  double charge_speed_fraction_;
  double charge_safety_envelope_speed_;
  std::uint64_t charge_active_duration_ticks_;
  std::uint64_t charge_hit_stun_duration_ticks_;
};

} // namespace blob_royale::gameplay

#endif
