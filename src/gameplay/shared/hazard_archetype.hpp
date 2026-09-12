#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_ARCHETYPE_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_HAZARD_ARCHETYPE_HPP

#include "motion_contact_observation.hpp"
#include "simulation_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace blob_royale::gameplay {

// canonical: hazard_archetype -- one configured kind of object that crosses the arena.
//
// **A hazard kind is configuration, not code.** One `[hazard.<kind>]` section declares one kind,
// the kind name is open, and adding a kind is that section and nothing else: no C++ file names a
// kind, no list of kinds is maintained anywhere, and no registry row is added.
// `tests/unit/application/application_config_loader_tests.cpp` proves it with a kind name that
// appears in no source file, and names that fact so a reader can `grep -r` it rather than trust it.
// The open name reaches this value through the loader's section-family concept
// (`src/application/application_config_loader.cpp`), which is the only thing in the configuration
// schema that is open at all.
//
// **There is no `[hazards]` section and no `kinds=` key, and the two omissions are one decision.**
// The family-wide section this feature was sketched with carried a spawn interval and a list naming
// the kinds in play. The list is redundant with the set of declared `[hazard.*]` sections -- two
// sources of truth for one list, which is exactly the ambiguity this tree's conventions forbid,
// and the failure mode is silent: a kind declared and not listed simply never spawns. Removing the
// list empties the section down to the interval, and an interval is better per kind anyway,
// because "a comet every six seconds and a boulder every twenty" is the first thing a designer
// asks for and a single cadence cannot express it. Removing the section also removes the question
// of whether it is required: an existing configuration that declares no hazard sections at all is
// still a complete configuration, which is what keeps `deploy/ubuntu-pc/blob-royale.cfg` and every
// fixture loading unchanged, and this tree does not have optional sections or silently defaulted
// keys (`royale/royale_configuration.hpp`). A family-wide density multiplier can be added later as
// its own section without invalidating anything authored today.
//
// **The archetype names no entry edge.** A hazard needs a reproducible entry point *and* direction,
// and both must come from the world's seeded generator so a replay reproduces the crossing
// (`docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for framework
// code"). Configuring the edge would make one component of that geometry authored and the rest
// drawn, which is a split source of truth for one decision; it would also need a closed edge
// vocabulary in C++, and a designer who wanted an edge the enumeration does not name would be back
// to writing code, which is the exact bar this value exists to clear. A fixed edge per kind is also
// worse play: every comet arriving from the left is a pattern to memorize rather than a hazard.
// The spawner therefore draws the edge, the point along it, and the crossing direction from one
// seeded stream. Adding an `entry_edge=` key later is additive, because the schema is closed and a
// key that is absent today cannot be silently ignored tomorrow.
//
// **It constructs no `PhysicsBody`.** This is pure validated data in the units a designer authors,
// and the translation into a body belongs to the spawn system that seats one. Keeping the two
// apart is what lets a hazard table be validated at startup by a process that has no world yet.
// related: ../game_mode_configuration.hpp -- where the validated table hangs.
// related: shared/duration_ticks.hpp -- the one conversion the spawn interval goes through.
// related: gameplay_validation_error.hpp -- the `GAMEPLAY.HAZARD_*` rejections.
class HazardArchetype final {
public:
  // `common.schema.json#/$defs/kind_name`: the grammar every published kind name already satisfies.
  // A hazard kind is a kind and a hazard is meant to be drawn, so a name that could never be
  // encoded is a startup rejection rather than a frame every client must close on. It is the same
  // rule `[match] mode` and every bot kind already pass, and it is that rule rather than a copy of
  // it -- the bound and the grammar both come from `blob_simulation`, which every layer that
  // accepts a kind name can reach (`src/simulation/snake_case_identity.hpp`).
  static constexpr std::size_t kMaximumKindNameLength = simulation::kMaximumKindNameLength;

  // The `[hazard.<kind>]` section as authored, one member per key plus the section's own open
  // instance name. Units are in the names because the value alone cannot carry them, which is the
  // convention of `deploy/ubuntu-pc/blob-royale.cfg`.
  struct Section final {
    // The instance name of the `[hazard.<kind>]` section, which no C++ file may name.
    std::string kind_name;
    double radius_world_units;
    // Relative to the unit mass every ordinary blob carries (`simulation/physics_body.hpp`), so a
    // mass of 40 is a body forty times harder to shove aside than a player. There is no absolute
    // mass unit in this game and the impulse equation reads only the ratio, so spelling one out in
    // the key name would be inventing a unit rather than naming one.
    double mass;
    // The coefficient of restitution, in `[0, 1]`: the fraction of normal closing speed a contact
    // returns. `1` is perfectly elastic and is the accepted baseline every ordinary blob carries;
    // `0` leaves the pair moving together along the normal. Dimensionless by definition, so there
    // is no unit to spell out.
    double restitution;
    double speed_world_units_per_second;
    // How often one hazard of this kind enters the arena. Authored in seconds like every other
    // duration in the configuration and converted to ticks exactly once, here.
    double spawn_interval_seconds;
    // Whether touching this hazard eliminates a player. The key names the contact rule it will
    // select rather than carrying a bare adjective, so a reader of the configuration knows which
    // rule a `true` here turns on.
    bool lethal_on_contact;
    // Required in authored configuration. Programmatic declarations without an override retain
    // the historical impact-only default; one created instance may override it.
    simulation::ContactEffectPolicy contact_effect_policy =
        simulation::ContactEffectPolicy::kClosingImpact;

    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates the authored section and converts its one duration. Throws GameplayValidationError
  // naming the exact configuration key that failed, in the `hazard.<kind>.<key>` form the section
  // is authored in, so the rejection can be read straight back onto a line of the file.
  //
  // The rules, checked in this order so a section with two mistakes always names the same one:
  // the kind name matches the published kind grammar; the radius is finite and strictly positive;
  // the mass is finite and strictly positive; the restitution is finite and within `[0, 1]`; the
  // speed is finite and strictly positive; and the spawn interval converts to at least one whole
  // tick. Zero is a rejection for all four scalars rather than a degenerate accepted value, because
  // a hazard with no size, no mass, or no speed is not a hazard, and an interval under half a tick
  // would spawn a body every tick until the entity budget ran out.
  [[nodiscard]] static HazardArchetype create(const Section& section);

  HazardArchetype(const HazardArchetype&) = default;
  HazardArchetype(HazardArchetype&&) noexcept = default;
  HazardArchetype& operator=(const HazardArchetype&) = default;
  HazardArchetype& operator=(HazardArchetype&&) noexcept = default;
  ~HazardArchetype() = default;

  // The declared kind, which is the instance name of the section that declared it.
  [[nodiscard]] const std::string& kind_name() const& noexcept { return kind_name_; }
  [[nodiscard]] const std::string& kind_name() const&& = delete;
  // wu. The disc radius the spawned body carries.
  [[nodiscard]] double radius() const noexcept { return radius_; }
  // Relative to the unit mass of an ordinary blob.
  [[nodiscard]] double mass() const noexcept { return mass_; }
  // The pair's coefficient of restitution contribution, in `[0, 1]`.
  [[nodiscard]] double restitution() const noexcept { return restitution_; }
  // wu/s. The magnitude of the velocity the spawner gives the body; the direction is drawn.
  [[nodiscard]] double speed() const noexcept { return speed_; }
  // Ticks between two hazards of this kind entering the arena. At least one.
  [[nodiscard]] std::uint64_t spawn_interval_ticks() const noexcept {
    return spawn_interval_ticks_;
  }
  // Whether contact with a player eliminates that player.
  [[nodiscard]] bool lethal_on_contact() const noexcept { return lethal_on_contact_; }
  [[nodiscard]] simulation::ContactEffectPolicy contact_effect_policy() const noexcept {
    return contact_effect_policy_;
  }

  friend bool operator==(const HazardArchetype&, const HazardArchetype&) = default;

private:
  HazardArchetype(std::string kind_name, double radius, double mass, double restitution,
                  double speed, std::uint64_t spawn_interval_ticks, bool lethal_on_contact,
                  simulation::ContactEffectPolicy contact_effect_policy);

  std::string kind_name_;
  double radius_;
  double mass_;
  double restitution_;
  double speed_;
  std::uint64_t spawn_interval_ticks_;
  bool lethal_on_contact_;
  simulation::ContactEffectPolicy contact_effect_policy_;
};

} // namespace blob_royale::gameplay

#endif
