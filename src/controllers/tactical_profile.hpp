#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP

#include "bot_profile_name.hpp"
#include "tactical_objective_candidates.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace blob_royale::controllers {

// canonical: tactical_objective_weights -- one authored weight per `TacticalObjectiveKind`.
//
// These are the settings that make one profile prefer the hill and another the road. Until they
// existed the profile was consulted at four call sites and *none of them was selection*, so two
// profiles differing only in numbers picked the same candidate on the same frame
// (`docs/reviews/2026-09-12-tactical-combat-preflight.md` § "Five structural blockers").
//
// **Authored as one key per kind**, not as one positional list. `[bot_profile.<name>]` is a closed
// flat key schema whose family is closed within an instance, so one key per kind makes an
// unauthored kind the parser's own `KEY_MISSING`, naming the key, before any domain rule runs. The
// alternative considered was `objective_weights=1,0.5,0.25,0.125`: one line instead of four in
// every authored file, but a kind's weight becomes a *position* rather than a name in each of them,
// a wrong-length list surfaces as a domain arity rejection instead of the parser's missing-key
// diagnostic, and a reader of the file cannot tell which number is which.
// `tactical_objective_weight_key` is what keeps the explicit form from costing a second source of
// truth: the parser's schema, the rejection contexts and the tests all read each key's name from
// here rather than spelling it again (`src/application/application_config_loader.cpp`
// `kConfigFamilyFieldSpecs`). A nested `[bot_profile.<name>.objective_weights]` block was rejected
// too -- the section-family machinery is exactly one level deep, and inventing a second level for
// four doubles would be a parser change with no other caller.
//
// **The set stays closed against the enum by the compiler, not by a comment.** The two functions
// below are switches with no `default` label over `TacticalObjectiveKind`, and every first-party
// target builds with `-Wall -Wextra -Werror` (root `CMakeLists.txt`), so a fifth objective kind is
// a build failure in this header before it can become a kind that every authored profile silently
// weights at zero. Nothing else in the tree closes that hole: the kind ordinal is a cast and the
// kind count is a constant, and neither notices an appended enumerator.
//
// related: tactical_objective_candidates.hpp -- the kind enum, its ordinal, and the scoring rule
// these weights feed. The dependency runs one way, profile -> kinds; that header must not include
// this one, or the two become a cycle. A stage needing both belongs in a third module above them.
struct TacticalObjectiveWeights final {
  double hill;
  double zone;
  double race_gate;
  double race_recovery;
  friend bool operator==(const TacticalObjectiveWeights&,
                         const TacticalObjectiveWeights&) = default;
};

// The authored key suffix of one kind's weight: the single name of that key, read by the
// configuration parser's closed schema, by this domain's rejection contexts, and by every test that
// authors a section.
[[nodiscard]] constexpr std::string_view
tactical_objective_weight_key(const TacticalObjectiveKind kind) noexcept {
  switch (kind) {
  case TacticalObjectiveKind::kHill:
    return "objective_weight_hill";
  case TacticalObjectiveKind::kZone:
    return "objective_weight_zone";
  case TacticalObjectiveKind::kRaceGate:
    return "objective_weight_race_gate";
  case TacticalObjectiveKind::kRaceRecovery:
    return "objective_weight_race_recovery";
  }
  return "objective_weight_invalid";
}

// One kind's authored weight. A value outside the enum -- reachable only by a cast, since the
// underlying type admits more values than the enum declares -- yields a quiet NaN rather than a
// plausible zero, so `TacticalProfile::create`'s finiteness rule rejects it loudly instead of
// letting it read as "this profile ignores that objective". Zero is a real authored answer here; it
// must not double as the error answer.
[[nodiscard]] constexpr double
tactical_objective_weight_of(const TacticalObjectiveWeights& weights,
                             const TacticalObjectiveKind kind) noexcept {
  switch (kind) {
  case TacticalObjectiveKind::kHill:
    return weights.hill;
  case TacticalObjectiveKind::kZone:
    return weights.zone;
  case TacticalObjectiveKind::kRaceGate:
    return weights.race_gate;
  case TacticalObjectiveKind::kRaceRecovery:
    return weights.race_recovery;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

// canonical: tactical_profile -- seven validated, active settings for one tactical algorithm,
// authored as ten keys of one `[bot_profile.<name>]` section.
//
// Every setting here is read by behaviour this step ships. Three more that ADR 0008 § "Tactical
// profiles" names -- aggression, charge appetite, shield timing error -- are deliberately absent:
// each needs a physics fact the wire does not publish, all three are Step 22b, and the ADR refuses
// an inert combat knob before its behavior exists. The fuzz seed that enforces that rule,
// `tests/fuzz/corpus/application/rejected-tactical-profile-inert-combat.cfg`, still points at
// `aggression`. `risk_tolerance` is not that knob under another name: it only scales a penalty away
// and never adds score, so no value of it can make a bot prefer danger (`controllers_limits.hpp`
// `kMaximumTacticalRiskTolerance`).
//
// The three settings the decision pipeline added:
//
// * `objective_weights` -- the per-kind preference term of the utility score. See
//   `TacticalObjectiveWeights` above for how they are authored and why.
// * `risk_tolerance` -- in `[0,1]`, how much of a screened-but-marginal candidate's penalty this
//   profile ignores. At zero the penalty applies in full and the profile passes such a candidate
//   over; at one it is cancelled and a marginal candidate ranks exactly as a clean one does.
// * `prediction_horizon_ticks` -- committed ticks a prediction may look ahead, bounded by
//   `kMaximumTacticalPredictionHorizonTicks`. Zero is an authored answer: that profile predicts
//   nothing, performs no prediction step, and reads only the published present.
// related: tactical_profile_catalogue.hpp -- the bounded, name-unique set of these.
class TacticalProfile final {
public:
  // The authored section, in declared key order. `create` validates in exactly this order, so a
  // section with two bad keys reports the first declared one, and the order matches
  // `kConfigFamilyFieldSpecs` in `src/application/application_config_loader.cpp` and the
  // initializer list in `parse_tactical_profiles` beside it. New keys append. Interleaving one
  // would change which key an existing multi-defect section blames, and would silently re-point
  // `tests/fuzz/corpus/application/rejected-tactical-profile-missing-key.cfg` at a different key
  // than the one its name states.
  struct Section final {
    std::string profile_name;
    double objective_seek_probability;
    std::uint64_t reaction_delay_ticks;
    double aim_error;
    std::uint64_t target_persistence_ticks;
    TacticalObjectiveWeights objective_weights;
    double risk_tolerance;
    std::uint64_t prediction_horizon_ticks;
    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates in authored order. Throws CONTROLLERS.TACTICAL_PROFILE_* with the failed key;
  // required key presence and lexical integer validation belong to the configuration parser.
  [[nodiscard]] static TacticalProfile create(const Section& section);
  [[nodiscard]] const simulation::BotProfileName& name() const& noexcept { return name_; }
  const simulation::BotProfileName& name() const&& = delete;
  [[nodiscard]] double objective_seek_probability() const noexcept {
    return objective_seek_probability_;
  }
  [[nodiscard]] std::uint64_t reaction_delay_ticks() const noexcept {
    return reaction_delay_ticks_;
  }
  [[nodiscard]] double aim_error() const noexcept { return aim_error_; }
  [[nodiscard]] std::uint64_t target_persistence_ticks() const noexcept {
    return target_persistence_ticks_;
  }
  [[nodiscard]] TacticalObjectiveWeights objective_weights() const noexcept {
    return objective_weights_;
  }
  // The weight this profile authored for one objective kind, which is what a selection stage reads.
  [[nodiscard]] double objective_weight(const TacticalObjectiveKind kind) const noexcept {
    return tactical_objective_weight_of(objective_weights_, kind);
  }
  [[nodiscard]] double risk_tolerance() const noexcept { return risk_tolerance_; }
  [[nodiscard]] std::uint64_t prediction_horizon_ticks() const noexcept {
    return prediction_horizon_ticks_;
  }
  friend bool operator==(const TacticalProfile&, const TacticalProfile&) = default;

private:
  TacticalProfile(simulation::BotProfileName name, const Section& section) noexcept;
  simulation::BotProfileName name_;
  double objective_seek_probability_;
  std::uint64_t reaction_delay_ticks_;
  double aim_error_;
  std::uint64_t target_persistence_ticks_;
  TacticalObjectiveWeights objective_weights_;
  double risk_tolerance_;
  std::uint64_t prediction_horizon_ticks_;
};

} // namespace blob_royale::controllers

#endif
