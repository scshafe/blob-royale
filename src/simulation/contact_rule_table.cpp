#include "contact_rule_table.hpp"

#include "contact_rule_name.hpp"
#include "game_world.hpp"
#include "physics.hpp"
#include "simulation_validation_error.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"

#include <string>
#include <utility>

namespace blob_royale::simulation {
namespace {

[[nodiscard]] const PhysicsBody* body_of(const GameWorld& world, const EntityId entity) noexcept {
  return world.store<PhysicsBody>().find(entity);
}

// Whether the row matches this ordered argument pair. Both predicates are evaluated against the
// committed world, which is where the structural properties a predicate may read -- the static
// flag, the collision masks, a component's presence -- are unchanged by phases 1 and 3.
[[nodiscard]] bool row_matches(const ContactRule& row, const GameWorld& world, const EntityId first,
                               const EntityId second) {
  return row.first_predicate()(world, first) && row.second_predicate()(world, second);
}

} // namespace

ContactRuleTable ContactRuleTable::create(std::vector<ContactRule> rows) {
  // Quadratic over the declared list on purpose, exactly as SystemPipeline is: a mode declares a
  // handful of rows, and scanning in declared order names the *first* row a duplicate collides
  // with, which is the row an author has to edit.
  for (std::size_t index = 1; index < rows.size(); ++index) {
    const std::string_view name = rows[index].name();
    for (std::size_t earlier = 0; earlier < index; ++earlier) {
      if (rows[earlier].name() != name) {
        continue;
      }
      throw SimulationValidationError(SimulationValidationCode::kContactRuleTableDuplicateRuleName,
                                      "contact_rule_table.rows.name",
                                      "contact rule name " + std::string(name) +
                                          " is already declared by row " + std::to_string(earlier) +
                                          " (declared row " + std::to_string(index) + ")");
    }
  }
  return ContactRuleTable{std::move(rows)};
}

ContactRuleTable ContactRuleTable::built_in() {
  std::vector<ContactRule> rows;
  rows.reserve(3);
  // Declared order is precedence. `variable_impulse` is first and is predicated on a body that
  // actually differs from the baseline, so it is reached only by a pair the accepted
  // equal-unit-mass equation is not written for; `elastic_disc` below is left holding exactly the
  // pairs it always held. The two orientations of the row cover both arrangements: `(variable,
  // baseline)` matches canonically and `(baseline, variable)` matches swapped, because the second
  // predicate is the plain dynamic test.
  rows.push_back(ContactRule::create(kVariableImpulseContactRuleName, body_is_variable_dynamic,
                                     body_is_dynamic, variable_impulse_response));
  rows.push_back(ContactRule::create(kElasticDiscContactRuleName, body_is_dynamic, body_is_dynamic,
                                     elastic_disc_response));
  rows.push_back(ContactRule::create(kReflectStaticContactRuleName, body_is_dynamic, body_is_static,
                                     reflect_static_response));
  return create(std::move(rows));
}

ContactRuleTable ContactRuleTable::with_rows_above_built_in(std::vector<ContactRule> rows) {
  // `built_in()` is called rather than reconstructed, so the baseline rows a mode inherits are the
  // same values `built_in()` returns and cannot drift from them. The caller's rows keep their
  // declared order and all of them precede all of the built-in ones.
  const ContactRuleTable built_in_table = built_in();
  const std::span<const ContactRule> built_in_rows = built_in_table.rows();
  rows.reserve(rows.size() + built_in_rows.size());
  rows.insert(rows.end(), built_in_rows.begin(), built_in_rows.end());
  return create(std::move(rows));
}

ContactRuleTable ContactRuleTable::empty() { return ContactRuleTable{{}}; }

std::optional<ContactRuleTable::Match>
ContactRuleTable::first_match(const GameWorld& world, const EntityId canonical_first,
                              const EntityId canonical_second) const {
  for (std::size_t row_index = 0; row_index < rows_.size(); ++row_index) {
    const ContactRule& row = rows_[row_index];
    if (row_matches(row, world, canonical_first, canonical_second)) {
      return Match{row_index, ContactOrientation::kCanonical};
    }
    if (row_matches(row, world, canonical_second, canonical_first)) {
      return Match{row_index, ContactOrientation::kSwapped};
    }
  }
  return std::nullopt;
}

ContactRuleTable::ContactRuleTable(std::vector<ContactRule> rows) noexcept
    : rows_(std::move(rows)) {}

bool body_is_dynamic(const GameWorld& world, const EntityId entity) {
  const PhysicsBody* body = body_of(world, entity);
  return body != nullptr && !body->is_static();
}

bool body_is_static(const GameWorld& world, const EntityId entity) {
  const PhysicsBody* body = body_of(world, entity);
  return body != nullptr && body->is_static();
}

bool body_is_variable_dynamic(const GameWorld& world, const EntityId entity) {
  const PhysicsBody* body = body_of(world, entity);
  return body != nullptr && !body->is_static() && !body_has_baseline_physics(*body);
}

ContactResponse elastic_disc_response(const ContactRule::Subject& first,
                                      const ContactRule::Subject& second,
                                      const PlayerPairContact& contact,
                                      const TickContext& context) {
  // The equation is not reimplemented here. `resolve_player_pair_collision` is the one accepted
  // narrow phase, and this row selects it; that is what makes the built-in row provably the
  // accepted baseline rather than a second transcription of it.
  //
  // The contact distance is `2 * player_radius` from the configuration rather than the sum of the
  // two bodies' `radius` fields: the accepted baseline carries one common radius on the
  // configuration, and moving it onto the body is a versioned physics change
  // (`docs/architecture/0003-deterministic-simulation-contract.md`
  // § "Justified extension points and what-if stress").
  const PlayerPairCollisionResult collision = resolve_player_pair_collision(
      first.body, second.body, context.simulation_config().player_radius());
  return ContactResponse::create(
      first.body.with_velocity(collision.first_velocity()),
      second.body.with_velocity(collision.second_velocity()),
      {WorldEvent{contact_event_of(first, second, contact,
                                   ContactRuleName::create(kElasticDiscContactRuleName))}});
}

ContactResponse variable_impulse_response(const ContactRule::Subject& first,
                                          const ContactRule::Subject& second,
                                          const PlayerPairContact& contact,
                                          const TickContext& context) {
  // As with `elastic_disc`, the equation is selected rather than transcribed: this row's whole
  // content is "use the general impulse instead of the equal-unit-mass exchange".
  //
  // **Unlike `elastic_disc`, this row measures the contact at the two bodies' own radii.**
  // `resolve_general_pair_collision` takes the configured radius as the *fallback* for a body that
  // declares no size, and `pair_contact_distance` is `r_a + r_b`. A hazard drawn at its own radius
  // has to collide at the edge a player can see; measuring a twenty-six unit boulder at twice a
  // twelve unit blob would let a player sink into the drawn rock before anything happened.
  //
  // This stays an addition rather than the versioned unequal-radii amendment ADR 0003
  // § "Justified extension points and what-if stress" names, and for the same reason per-body mass
  // does: the general equation is reachable only through this row, and this row is reachable only
  // when a body differs from the baseline. `elastic_disc` still calls
  // `resolve_player_pair_collision` with `2 * player_radius` and keeps its exact arithmetic, so no
  // accepted horizon moves. Making per-body radius the measure for *every* body -- the growing-blob
  // change -- is still that versioned amendment, because it would move the baseline itself.
  //
  // The two phases that remain on the configured radius are the kernel's own: phase 3's
  // narrow-phase gate and the broad phase's coverage box both still measure every pair at
  // `2 * player_radius`, so a body larger than the configured radius is admitted and indexed as if
  // it were configured-sized. That bounds how large a hazard this row can usefully resolve until
  // those two follow.
  const PlayerPairCollisionResult collision = resolve_general_pair_collision(
      first.body, second.body, context.simulation_config().player_radius());
  return ContactResponse::create(
      first.body.with_velocity(collision.first_velocity()),
      second.body.with_velocity(collision.second_velocity()),
      {WorldEvent{contact_event_of(first, second, contact,
                                   ContactRuleName::create(kVariableImpulseContactRuleName))}});
}

ContactResponse reflect_static_response(const ContactRule::Subject& first,
                                        const ContactRule::Subject& second,
                                        const PlayerPairContact& contact, const TickContext&) {
  // `v' = v - 2 (v . n) n` with `n` the row-orientation normal, which negates the normal component
  // and leaves the tangential component attached to the moving body. That is ADR 0003
  // § "Wall policy"'s "preserves speed magnitude on the reflected axis and leaves the other
  // component unchanged", written for an arbitrary normal instead of an axis-aligned edge.
  const Vector2& velocity = first.body.velocity();
  const Vector2& normal = contact.normal();
  const double normal_speed = (velocity.x() * normal.x()) + (velocity.y() * normal.y());
  const Vector2 reflected_velocity =
      Vector2::create(velocity.x() - (2.0 * normal_speed * normal.x()),
                      velocity.y() - (2.0 * normal_speed * normal.y()));
  return ContactResponse::create(
      first.body.with_velocity(reflected_velocity), second.body,
      {WorldEvent{contact_event_of(first, second, contact,
                                   ContactRuleName::create(kReflectStaticContactRuleName))}});
}

} // namespace blob_royale::simulation
