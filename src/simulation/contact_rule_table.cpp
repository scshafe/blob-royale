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

PairMotionResponse<WorldEvent> ContactRuleTable::respond(const GameWorld& world,
                                                         const ContactRule::Subject& first,
                                                         const ContactRule::Subject& second,
                                                         const PairContactObservation& observation,
                                                         const TickContext& context) const {
  PairMotionResponse<WorldEvent> result{{first.body}, {second.body}, {}};
  const auto match = first_match(world, first.entity, second.entity);
  if (!match) {
    return result;
  }
  const bool swapped = match->orientation == ContactOrientation::kSwapped;
  const auto row_observation = swapped ? observation.reversed() : observation;
  const auto response = rows_[match->row_index].response()(
      world, swapped ? second : first, swapped ? first : second, row_observation, context);
  if (response.replaces_bodies()) {
    result.first = swapped ? response.second_result() : response.first_result();
    result.second = swapped ? response.first_result() : response.second_result();
  }
  result.effects.assign(response.events().begin(), response.events().end());
  return result;
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

ContactResponse elastic_disc_response(const GameWorld&, const ContactRule::Subject& first,
                                      const ContactRule::Subject& second,
                                      const PairContactObservation& observation,
                                      const TickContext&) {
  auto first_body = first.body;
  auto second_body = second.body;
  if (observation.impact) {
    const auto collision =
        resolve_player_pair_collision(first.body, second.body, *observation.impact);
    first_body = first_body.with_velocity(collision.first_velocity());
    second_body = second_body.with_velocity(collision.second_velocity());
  }
  return ContactResponse::create(
      {first_body}, {second_body},
      {WorldEvent{contact_event_of(first, second, observation.touch,
                                   ContactRuleName::create(kElasticDiscContactRuleName))}});
}

ContactResponse variable_impulse_response(const GameWorld&, const ContactRule::Subject& first,
                                          const ContactRule::Subject& second,
                                          const PairContactObservation& observation,
                                          const TickContext&) {
  auto first_body = first.body;
  auto second_body = second.body;
  if (observation.impact) {
    const auto collision =
        resolve_general_pair_collision(first.body, second.body, *observation.impact);
    first_body = first_body.with_velocity(collision.first_velocity());
    second_body = second_body.with_velocity(collision.second_velocity());
  }
  return ContactResponse::create(
      {first_body}, {second_body},
      {WorldEvent{contact_event_of(first, second, observation.touch,
                                   ContactRuleName::create(kVariableImpulseContactRuleName))}});
}

ContactResponse reflect_static_response(const GameWorld&, const ContactRule::Subject& first,
                                        const ContactRule::Subject& second,
                                        const PairContactObservation& observation,
                                        const TickContext&) {
  auto first_body = first.body;
  if (observation.impact) {
    first_body = first_body.with_velocity(
        reflect_static_contact_velocity(first.body.velocity(), observation.impact->normal()));
  }
  return ContactResponse::create(
      {first_body}, {second.body},
      {WorldEvent{contact_event_of(first, second, observation.touch,
                                   ContactRuleName::create(kReflectStaticContactRuleName))}});
}

} // namespace blob_royale::simulation
