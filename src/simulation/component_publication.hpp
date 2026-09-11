#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_PUBLICATION_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_PUBLICATION_HPP

#include <utility>

namespace blob_royale::simulation {

// canonical: component_publication -- what one component kind publishes in a snapshot.
//
// A snapshot is the value that leaves the simulation: the protocol encodes it and, from Step 23,
// an in-process bot reads it. Most component kinds publish themselves verbatim, which is what the
// primary template says, and a kind that carries private or tick-local state specializes this
// template beside its own struct so the rule lives with the field it is about rather than in the
// snapshot builder (`docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol
// shape").
//
// This is the resolution of engine review finding 4. `Controllable::commands_this_tick` is this
// tick's recorded input for one entity; publishing it hands every reader of a snapshot every
// player's live input for the tick being rendered, which is precisely what protocol v2 withholds
// from the wire and which would break the human/bot symmetry in the bot's favour. Stripping at
// the publication boundary rather than clearing at commit keeps the in-tick contract exactly as
// ADR 0003 § "Canonical tick" phase 0 and ADR 0004 § "Commands" wrote it -- a system still reads
// what phase 0 recorded, and the committed world still holds it for an oracle or a diagnostic --
// while making the leak structurally impossible at the one place it could leave.
//
// The projection takes and returns by value so a stripping specialization discards the state
// instead of copying it: publishing a Controllable now costs no vector copy at all.
// related: components/controllable_component.hpp -- the one specialization today.
// related: world_snapshot.hpp -- the only caller.
template <typename Component> struct ComponentPublication {
  [[nodiscard]] static Component published(Component value) { return value; }
};

// The published projection of one component value.
template <typename Component> [[nodiscard]] Component published_component(Component value) {
  return ComponentPublication<Component>::published(std::move(value));
}

} // namespace blob_royale::simulation

#endif
