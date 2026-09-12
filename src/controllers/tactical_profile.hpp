#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP

#include "bot_profile_name.hpp"
#include "tactical_objective_candidates.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace blob_royale::controllers {

// canonical: authored_objective_weight -- a weight no construction site can leave out.
//
// **A bare `double` member has a silent default, and here that default is a legal value.** C++
// fills an omitted designated initializer, and every trailing member of a short positional one, by
// value-initializing to zero rather than by refusing to compile -- and zero is a real authored
// weight, so the omission survives `TacticalProfile::create`, whose all-zero rejection fires only
// when *every* weight is zero. That is not hypothetical: Step 22a burned a repair on exactly this
// class of defect, on construction sites that appeared in no inventory
// (`docs/reviews/2026-09-12-tactical-combat-contract.md` § "Everything that must gain the three
// keys"). Deleting the default constructor turns each omission into a build failure at the site
// that made it, which is the same discipline the no-`default` switches below apply to the enum: the
// compiler closes the set, not a comment and not a reviewer. A designated initializer is what a
// site should write -- it names each weight, so a new kind cannot be authored into its neighbour's
// place either.
//
// **The converting constructor is deliberately implicit and deliberately does not validate.** This
// type has exactly one job, and range checking is not it: a number outside `[0,1]` must stay a
// named `CONTROLLERS.TACTICAL_PROFILE_OBJECTIVE_WEIGHT_INVALID` naming the failed key, thrown by
// `TacticalProfile::create` where every other authored bound is checked, rather than a throw out of
// an aggregate initializer that can name nothing. Reading is explicit -- `value()`, the same
// spelling `MotionTime` uses -- because there is no conversion operator: one would make
// `double == AuthoredObjectiveWeight` ambiguous against the defaulted equality below, and this
// domain has no arithmetic that wants a weight to decay silently into a `double`.
class AuthoredObjectiveWeight final {
public:
  AuthoredObjectiveWeight() = delete;
  constexpr AuthoredObjectiveWeight(const double weight) noexcept : weight_(weight) {}
  [[nodiscard]] constexpr double value() const noexcept { return weight_; }
  friend bool operator==(const AuthoredObjectiveWeight&, const AuthoredObjectiveWeight&) = default;

private:
  double weight_;
};

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
// alternative considered was `objective_weights=1,0.5,0.25,0.125,0`: one line instead of five in
// every authored file, but a kind's weight becomes a *position* rather than a name in each of them,
// a wrong-length list surfaces as a domain arity rejection instead of the parser's missing-key
// diagnostic, and a reader of the file cannot tell which number is which.
// `tactical_objective_weight_key` is what keeps the explicit form from costing a second source of
// truth: the parser's schema, the rejection contexts and the tests all read each key's name from
// here rather than spelling it again (`src/application/application_config_loader.cpp`
// `kConfigFamilyFieldSpecs`). A nested `[bot_profile.<name>.objective_weights]` block was rejected
// too -- the section-family machinery is exactly one level deep, and inventing a second level for
// five doubles would be a parser change with no other caller.
//
// **The set stays closed against the enum by the compiler, not by a comment.** The two functions
// below are switches with no `default` label over `TacticalObjectiveKind`, and every first-party
// target builds with `-Wall -Wextra -Werror` (root `CMakeLists.txt`), so a new objective kind is a
// build failure in this header before it can become a kind that every authored profile silently
// weights at zero. Nothing else in the tree closes that hole: the kind ordinal is a cast and the
// kind count is a constant, and neither notices an appended enumerator.
//
// related: tactical_objective_candidates.hpp -- the kind enum, its ordinal, and the scoring rule
// these weights feed. The dependency runs one way, profile -> kinds; that header must not include
// this one, or the two become a cycle. A stage needing both belongs in a third module above them.
// related: AuthoredObjectiveWeight above -- why no member here is a bare `double`.
struct TacticalObjectiveWeights final {
  AuthoredObjectiveWeight hill;
  AuthoredObjectiveWeight zone;
  AuthoredObjectiveWeight race_gate;
  AuthoredObjectiveWeight race_recovery;
  AuthoredObjectiveWeight shove_setup;
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
  case TacticalObjectiveKind::kShoveSetup:
    return "objective_weight_shove_setup";
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
    return weights.hill.value();
  case TacticalObjectiveKind::kZone:
    return weights.zone.value();
  case TacticalObjectiveKind::kRaceGate:
    return weights.race_gate.value();
  case TacticalObjectiveKind::kRaceRecovery:
    return weights.race_recovery.value();
  case TacticalObjectiveKind::kShoveSetup:
    return weights.shove_setup.value();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

// canonical: tactical_profile -- nine validated, active settings for one tactical algorithm,
// authored as thirteen keys of one `[bot_profile.<name>]` section.
//
// Every setting here is read by behaviour landing in the same commit that adds it, which is ADR
// 0008's legality test for a profile key: the ADR refuses an inert combat knob before its behaviour
// exists.
//
// **There is no `aggression` key, and that is the design rather than an omission.** ADR 0008 lists
// aggression as a *concept* a profile configures, not a key name, and the concept is already fully
// spent: its preference half is `objective_weight_shove_setup` under the one-key-per-kind rule
// above, and its danger-appetite half is precisely what `controllers_limits.hpp`
// `kMaximumTacticalRiskTolerance` forbids -- risk tolerance is built one-signed so that no value of
// it adds score to a dangerous candidate, and a second knob that did would leave two settings
// fighting over one term of `tactical_candidate_score`. The other two concepts that section names
// are realized here rather than as keys of their own: charge appetite is
// `charge_screen_diagonal_fraction`, and shield timing error is `shield_anticipation_ticks`, a
// one-signed lead window rather than a signed error
// (`docs/reviews/2026-09-12-tactical-combat-contract.md` § "Three new profile settings").
//
// **What keeps `aggression` from existing is a unit-test row, not the fuzz seed.**
// `tests/fuzz/corpus/application/rejected-tactical-profile-inert-combat.cfg` does still author
// `aggression=1`, and an earlier revision of this comment called that seed the guard. It is not:
// `verify-fuzz-regressions` replays a corpus member and asserts only that the target does not
// crash, and `application_config_fuzzer.cpp` catches every typed loader error and returns zero, so
// a seed that started being *accepted* would still pass. The live guard is the
// `ParserFailure{"aim_error=0.05", "aggression=0.05", kConfigurationKeyUnknown}` row in
// `tests/unit/application/fixtures/tactical_profile_configuration_fixture.hpp`, which asserts the
// key is refused by name.
//
// The five settings the decision pipeline and this step's combat behaviour added:
//
// * `objective_weights` -- the per-kind preference term of the utility score. See
//   `TacticalObjectiveWeights` above for how they are authored and why.
// * `risk_tolerance` -- in `[0,1]`, how much of a screened-but-marginal candidate's penalty this
//   profile ignores. At zero the penalty applies in full and the profile passes such a candidate
//   over; at one it is cancelled and a marginal candidate ranks exactly as a clean one does.
// * `prediction_horizon_ticks` -- committed ticks a prediction may look ahead, bounded by
//   `kMaximumTacticalPredictionHorizonTicks`. Zero is an authored answer: that profile predicts
//   nothing, performs no prediction step, and reads only the published present.
// * `charge_screen_diagonal_fraction` -- in `[0,1]`, **a fraction of the observed arena diagonal,
//   never world units**. `Observation::terrain()` already hands the controller validated bounds, so
//   the fraction costs no second geometry owner; an absolute scalar would mean two orders of
//   magnitude of different things across the configurations already in this tree, and one is a real
//   ceiling rather than an arbitrary stop -- a screen the size of the whole map is the strictest a
//   profile can author, because a longer ray can only find more ground endings -- where
//   `kMaximumWorldDimension` bounds nothing a profile author could reason about. **Its zero is the
//   permissive end, which is the opposite of the zero above, and the two read alike:** a screen of
//   zero length can find no exit, so that profile charges wherever the rest of the gate admits it.
//   What the screen answers is "is there ground under the corridor I am about to cross"; it is
//   never a claim the body can stop before leaving that corridor, and it cannot become one, because
//   `charge_speed_fraction` is the one ability number that reaches no snapshot -- a bot cannot
//   compute its own post-burst speed even in principle.
// * `shield_anticipation_ticks` -- committed ticks of lead on a predicted close, bounded by
//   `kMaximumTacticalShieldAnticipationTicks`. Zero is an authored answer exactly as it is for the
//   horizon above: that profile never anticipates and raises no speculative shield. It is a
//   *window*, so it is one-signed -- the two-sidedness ADR 0008 promises comes from the one-to-
//   twenty-one-tick jitter of the decision cadence, not from a signed timing-error key. The
//   prediction under it carries no drag term, because `drag_per_second` reaches no snapshot, so it
//   is biased early wherever drag is nonzero. **A window longer than the mode's perfect opening can
//   produce no parry at all, and keeping that relationship is the author's job**:
//   `blob_controllers` links only `blob_runtime` and `blob_simulation`, `AbilityConfiguration`
//   lives in `blob_gameplay`, and writing `[abilities] shield_perfect_window_seconds` into a
//   controller bound would be a second authoring home for a value an operator retunes.
// related: tactical_profile_catalogue.hpp -- the bounded, name-unique set of these.
class TacticalProfile final {
public:
  // The authored section, in declared key order. `create` validates in exactly this order, so a
  // section with two bad keys reports the first declared one, and the order matches
  // `kConfigFamilyFieldSpecs` in `src/application/application_config_loader.cpp` and the
  // initializer list in `parse_tactical_profiles` beside it. New keys append. Interleaving one
  // would change which key an existing multi-defect section blames, and would silently re-point
  // `tests/fuzz/corpus/application/rejected-tactical-profile-missing-key.cfg` at a different key
  // than the one its name states. The one place a key is *not* appended is inside
  // `objective_weights`, which stays in `TacticalObjectiveKind` ordinal order end to end -- the
  // validation loop walks ordinals, so a weight declared out of that order would blame a different
  // kind than the one at fault.
  struct Section final {
    std::string profile_name;
    double objective_seek_probability;
    std::uint64_t reaction_delay_ticks;
    double aim_error;
    std::uint64_t target_persistence_ticks;
    TacticalObjectiveWeights objective_weights;
    double risk_tolerance;
    std::uint64_t prediction_horizon_ticks;
    double charge_screen_diagonal_fraction;
    std::uint64_t shield_anticipation_ticks;
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
  [[nodiscard]] double charge_screen_diagonal_fraction() const noexcept {
    return charge_screen_diagonal_fraction_;
  }
  [[nodiscard]] std::uint64_t shield_anticipation_ticks() const noexcept {
    return shield_anticipation_ticks_;
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
  double charge_screen_diagonal_fraction_;
  std::uint64_t shield_anticipation_ticks_;
};

} // namespace blob_royale::controllers

#endif
