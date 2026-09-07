#ifndef BLOB_ROYALE_CONTROLLERS_WANDERER_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_WANDERER_CONTROLLER_HPP

#include "command_registry.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "deterministic_random.hpp"
#include "observation.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: wanderer_controller -- a seeded random heading, held for a personality's reaction
// delay.
// @extension-point controller
//
// The first registered bot, and the simplest thing that is still a real command source: it asks for
// a body when it has none, then draws a heading from its own generator, thrusts along it, and holds
// it for `reaction_delay_frames` decision passes before drawing again.
//
// **It is reproducible on its own, and that is the point ADR 0004 makes with it**
// (`docs/architecture/0004-gameplay-architecture.md` § "Controllers": "A deterministic bot owns a
// `DeterministicRandom` seeded from match configuration and is reproducible on its own"). It owns
// one `DeterministicRandom` -- the tree's one generator, not an equivalent -- so the whole command
// sequence is a function of `(seed, personality, the observations it was given)`. It reads no
// clock, no global, and no container ordering.
//
// **Determinism here is a convenience, not a contract.** A controller runs outside the tick, so
// ADR 0003's bit-identity rule does not bind it: replay replays the recorded command log, not the
// controller (`docs/architecture/0002-simulation-architecture.md` § "Consequences"). What the seed
// buys is a reproducible *bug report* and a fixture that does not need a scripted log to be stable.
//
// **A personality is constructor configuration, not a subclass.** A cautious wanderer and a twitchy
// one are two `Personality` values, not two classes; the same holds for
// `ChaserController::Personality`. That is the rule ADR 0004 states -- "Personalities are
// controller configuration values -- an aggression weight, a reaction delay in presentation frames,
// a target-selection bias -- not new types" -- and it is why this file has one class. related:
// controller_registry.hpp -- the row that resolves "wanderer". related: chaser_controller.hpp --
// the second registered bot. related: ../simulation/deterministic_random.hpp -- the one generator,
// reused rather than copied.
class WandererController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "wanderer";

  // Eight presentation frames at the 20 Hz presentation cadence protocol v2 pushes at is about
  // four tenths of a second of committed heading, which reads as a wander rather than a jitter.
  static constexpr std::uint32_t kDefaultReactionDelayFrames = 8;

  // One wanderer's tunable behavior. Add a field here to give every wanderer a new dimension; do
  // not add a class to give one wanderer a new mood.
  struct Personality final {
    // Decision passes one drawn heading is held before a new one is drawn. `0` draws a fresh
    // heading every pass; `kMaximumWandererReactionDelayFrames` is the accepted ceiling.
    std::uint32_t reaction_delay_frames{kDefaultReactionDelayFrames};
  };

  // The registry factory shape: a durable identity and a seed, with the declared default
  // personality.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          std::uint64_t seed);

  // The configured form. Throws ControllersValidationError with
  // `CONTROLLERS.WANDERER_REACTION_DELAY_OUT_OF_RANGE` above `kMaximumWandererReactionDelayFrames`,
  // because a roster value that made a bot decide once and then never again would look exactly like
  // a hung bot.
  [[nodiscard]] static std::unique_ptr<Controller>
  create(simulation::ControllerId controller, std::uint64_t seed, Personality personality);

  // Public because `create` hands the controller over as a `std::unique_ptr<Controller>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the validating entry point**;
  // this one takes the personality as given.
  WandererController(simulation::ControllerId controller, std::uint64_t seed,
                     Personality personality) noexcept;

  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }

  [[nodiscard]] std::uint64_t seed() const noexcept { return random_.seed(); }

  // Draws taken so far. Two wanderers that diverged in how many draws they took are already
  // unequal, which is the same reason `WorldSnapshot` commits the world generator's draw count.
  [[nodiscard]] std::uint64_t draw_count() const noexcept { return random_.draw_count(); }

  [[nodiscard]] const Personality& personality() const& noexcept { return personality_; }
  [[nodiscard]] const Personality& personality() const&& = delete;

  // The heading this wanderer is currently committed to. Zero before its first draw.
  [[nodiscard]] const simulation::Vector2& heading() const& noexcept { return heading_; }
  [[nodiscard]] const simulation::Vector2& heading() const&& = delete;

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;

  [[nodiscard]] simulation::Vector2 draw_heading();

  simulation::DeterministicRandom random_;
  Personality personality_;
  simulation::Vector2 heading_;
  std::uint32_t passes_until_new_heading_{0};
};

} // namespace blob_royale::controllers

#endif
