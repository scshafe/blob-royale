#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_KIND_NAME_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_KIND_NAME_HPP

#include <string_view>

namespace blob_royale::simulation {

// canonical: component_kind_name -- the one wire name of one component kind.
//
// Every component header specializes this template beside its own struct, so the registry never
// has to be told anything the component header does not already say. The primary template is
// declared and never defined: a kind that forgot its name fails to compile at the use site
// instead of publishing an empty name.
// related: component_registry.hpp -- the closed list of kinds that carry these names.
template <typename Component> struct ComponentKindName;

// The declared wire name of one component kind, for encoders, diagnostics, and fixtures.
template <typename Component>
inline constexpr std::string_view component_kind_name = ComponentKindName<Component>::value;

} // namespace blob_royale::simulation

#endif
