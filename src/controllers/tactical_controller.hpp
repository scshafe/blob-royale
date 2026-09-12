#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_CONTROLLER_HPP

#include "controller.hpp"
#include "deterministic_random.hpp"
#include "tactical_objective_candidates.hpp"
#include "tactical_profile.hpp"
#include "tactical_seed_identity.hpp"
#include "tick_window.hpp"

#include <memory>
#include <optional>

namespace blob_royale::controllers {

// canonical: tactical_controller -- public objective choice, absolute timing, and bounded aim
// error. One algorithm for all profiles; no combat, private schedules, future ticks, or pathfinder.
class TacticalController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "tactical";
  // Copies the profile and authored identity. Invalid identity throws CONTROLLERS.*.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          const TacticalProfile& profile,
                                                          TacticalSeedIdentity identity);
  TacticalController(simulation::ControllerId controller, TacticalProfile profile,
                     TacticalSeedIdentity identity);
  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }
  [[nodiscard]] const TacticalProfile& profile() const& noexcept { return profile_; }
  const TacticalProfile& profile() const&& = delete;
  [[nodiscard]] TacticalSeedIdentity seed_identity() const noexcept { return identity_; }
  [[nodiscard]] std::uint64_t draw_count() const noexcept {
    return state_.random ? state_.random->draw_count() : 0;
  }
  [[nodiscard]] std::optional<std::uint64_t> current_seed() const noexcept {
    return state_.random ? std::optional{state_.random->seed()} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::TickSequence> last_completed_tick() const noexcept {
    return state_.last_completed_tick;
  }
  [[nodiscard]] std::optional<TacticalObjectiveKey> target_key() const noexcept {
    return state_.lease ? std::optional{state_.lease->candidate.key} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::Vector2> target() const noexcept {
    return state_.lease ? std::optional{state_.lease->candidate.target} : std::nullopt;
  }
  [[nodiscard]] std::optional<simulation::TickWindow> reaction_window() const noexcept {
    return state_.reaction;
  }
  [[nodiscard]] std::optional<simulation::TickWindow> persistence_window() const noexcept {
    return state_.lease ? std::optional{state_.lease->window} : std::nullopt;
  }

protected:
  [[nodiscard]] bool accepts_observation(const Observation& observation) const noexcept override;

private:
  struct BodyIdentity final {
    simulation::EntityId entity;
    std::optional<simulation::TickSequence> generation;
    friend bool operator==(const BodyIdentity&, const BodyIdentity&) = default;
  };
  struct Lease final {
    TacticalObjectiveCandidate candidate;
    simulation::TickWindow window;
  };
  struct State final {
    std::optional<simulation::TickSequence> last_completed_tick{};
    std::optional<simulation::TickSequence> seeded_running_tick{};
    std::optional<simulation::DeterministicRandom> random{};
    std::optional<BodyIdentity> body{};
    bool observed_stun{false};
    std::optional<simulation::TickWindow> reaction{};
    std::optional<Lease> lease{};
    std::optional<simulation::Vector2> held_direction{};
  };
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;
  [[nodiscard]] std::vector<simulation::Command> decide_next(const Observation& observation,
                                                             State& next);
  static void clear_work(State& state) noexcept;

  TacticalProfile profile_;
  TacticalSeedIdentity identity_;
  State state_{};
};

} // namespace blob_royale::controllers

#endif
