#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_CONTROLLABLE_COMPONENT_HPP

#include "command_registry.hpp"
#include "component_kind_name.hpp"
#include "component_publication.hpp"
#include "controller_id.hpp"
#include "vector2.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

// canonical: controllable_component -- the link from one entity to the controller deciding for it.
//
// The simulation stores the controller id and never branches on it, so a human session and a bot
// are indistinguishable to a tick. This component is the only place the EntityId and ControllerId
// identity spaces meet.
//
// Private normalized intent persists between commands so shared locomotion can recompute against
// room tuning. Absence preserves authored acceleration; an explicit zero is held coast. Ability
// state still belongs in its own components (ADR 0004, Step 10 amendment).
struct Controllable final {
  ControllerId controller_id;
  // This tick's recorded commands for this entity: at most one of each kind, **in the tick's one
  // canonical order**, which is phase 0's application order and therefore the order
  // `InputBatch::commands()` already arrives in (`input_batch.hpp`). There is no second
  // convention: a recorded list used to be re-sorted by ascending CommandKind after phase 0 wrote
  // it, which was a no-op over the rank table it claimed to be independent of and left one closed
  // vocabulary with two orderings (engine review finding 6). Phase 0 records and does not
  // interpret, because command meaning is a system's job. The default member initializer keeps
  // `Controllable{controller_id}` -- the shape every existing seating and fixture site uses -- a
  // complete aggregate initialization.
  //
  // **This field is tick-local and is never published.** It is one entity's live input for the
  // tick being committed, so a snapshot carrying it would hand every reader every player's input
  // for the tick it is rendering -- the field protocol v2 deliberately withholds from the wire,
  // and a break of the human/bot symmetry in the bot's favour. The ComponentPublication
  // specialization below is what strips it, so the rule lives with the field rather than in the
  // snapshot builder (engine review finding 4).
  std::vector<Command> commands_this_tick{};
  std::optional<Vector2> normalized_thrust_intent{};

  friend bool operator==(const Controllable&, const Controllable&) = default;
};

template <> struct ComponentKindName<Controllable> {
  static constexpr std::string_view value = "controllable";
};

// A published Controllable is the controller link and nothing else: both recorded commands and
// persistent normalized intent are private. Discarding rather than copying the vector also
// removes the per-entity allocation a publication used to pay.
// related: component_publication.hpp -- why a kind declares this beside its own struct.
template <> struct ComponentPublication<Controllable> {
  [[nodiscard]] static Controllable published(Controllable value) {
    return Controllable{value.controller_id};
  }
};

} // namespace blob_royale::simulation

#endif
