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
  rows.reserve(2);
  rows.push_back(ContactRule::create(kElasticDiscContactRuleName, body_is_dynamic, body_is_dynamic,
                                     elastic_disc_response));
  rows.push_back(ContactRule::create(kReflectStaticContactRuleName, body_is_dynamic, body_is_static,
                                     reflect_static_response));
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
