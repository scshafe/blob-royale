#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_LETHAL_ON_CONTACT_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_LETHAL_ON_CONTACT_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: lethal_on_contact_component -- touching this entity eliminates a player.
//
// **The first component whose presence is its entire value.** It carries no field and cannot carry
// one: "lethal" is not a quantity, a duration, or a strength, and every question a field could
// answer -- how much damage, for how long, to whom -- is a question this game does not ask. A
// `bool lethal` member would be a field that can only ever hold `true`, because an entity that is
// not lethal simply does not carry the kind; storing the negative would give one world state two
// spellings, which is the same defect `ZoneExposure` avoids by erasing rather than storing zero.
//
// It is a component rather than a `PhysicsBody` flag because lethality is a **game rule** and a
// body is **mechanism**. `PhysicsBody` is the value the kernel's phases read and write, and no
// accepted phase has any business knowing that a contact eliminates someone; the kernel would then
// carry a rule that only one mode plays. Keeping it a separate kind is also what lets a mode attach
// lethality to something that is not a hazard at all -- a spike wall, a scoring pad that kills on a
// second touch -- without any of them being a new body field.
//
// **Absence is the whole of "harmless".** A blob, a wall, and a merely heavy boulder all carry no
// `LethalOnContact`, and the guarded composition's lethality predicate is exactly its presence, so
// a mode that declares no lethal archetype never reaches that branch at all
// (`src/gameplay/shared/guarded_pair_contact.hpp`). Plan Step 18 folded the standalone
// `lethal_hazard` row into that one pair-symmetric composition; the predicate and the diagnostic
// name it publishes moved unchanged, so nothing this comment claims about the marker changed.
//
// Being a registered kind makes it snapshot-visible, which is the point: a client has to be able to
// draw a hazard as dangerous *before* it arrives, and a renderer that had to infer lethality from
// radius or speed would be guessing at a rule the server already knows.
// related: ../../gameplay/shared/guarded_pair_contact.hpp -- the predicate that reads it.
// related: ../../gameplay/shared/guarded_pair_contact_rule.hpp -- the one row that composes it.
// related: component_registry.hpp -- the closed list this kind is registered in.
// related: docs/protocol/schema/v2/lethal-on-contact-component.schema.json -- the empty wire
// object.
struct LethalOnContact final {
  friend bool operator==(const LethalOnContact&, const LethalOnContact&) = default;
};

template <> struct ComponentKindName<LethalOnContact> {
  static constexpr std::string_view value = "lethal_on_contact";
};

} // namespace blob_royale::simulation

#endif
