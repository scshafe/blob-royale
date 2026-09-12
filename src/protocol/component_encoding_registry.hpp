#ifndef BLOB_ROYALE_PROTOCOL_COMPONENT_ENCODING_REGISTRY_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENT_ENCODING_REGISTRY_HPP

#include "component_encoding.hpp"
#include "components/charge_component_encoding.hpp"
#include "components/contact_effect_admission_component_encoding.hpp"
#include "components/controllable_component_encoding.hpp"
#include "components/hill_component_encoding.hpp"
#include "components/hill_motion_component_encoding.hpp"
#include "components/hill_presence_component_encoding.hpp"
#include "components/lethal_on_contact_component_encoding.hpp"
#include "components/lifetime_component_encoding.hpp"
#include "components/physics_body_component_encoding.hpp"
#include "components/race_progress_component_encoding.hpp"
#include "components/respawn_timer_component_encoding.hpp"
#include "components/score_component_encoding.hpp"
#include "components/shield_component_encoding.hpp"
#include "components/stun_component_encoding.hpp"
#include "components/team_component_encoding.hpp"
#include "components/zone_component_encoding.hpp"
#include "components/zone_exposure_component_encoding.hpp"
#include "protocol_v3_constants.hpp"

#include "component_kind_name.hpp"
#include "component_registry.hpp"

#include <cstddef>
#include <string_view>
#include <type_traits>

namespace blob_royale::protocol {

// canonical: component_encoding_registry -- the one place the per-kind wire encoders are gathered.
//
// It holds no list of its own. The kinds come from `simulation::ComponentRegistry` and each kind's
// wire name comes from its own `ComponentKindName`, so this file's whole content is the includes
// that make the specializations visible plus the two compile-time checks below. That is what makes
// "a new component kind is a new file plus one registration" true on the wire as well as in the
// world: the registration is the include, and everything else is generated.
// related: component_encoding.hpp -- the seam each included header specializes.
// related: src/simulation/component_registry.hpp -- the closed list this is generated from.

namespace detail {

// Whether every registered kind declares a wire encoding. `ComponentWireEncoding` is declared and
// never defined, so an unspecialized kind is an incomplete type here and this is `false`; the
// static_assert below then names the omission at compile time instead of leaving the snapshot
// encoder to fail with a template instantiation wall.
template <typename Component, typename = void> inline constexpr bool kHasWireEncoding = false;
template <typename Component>
inline constexpr bool
    kHasWireEncoding<Component, std::void_t<decltype(sizeof(ComponentWireEncoding<Component>))>> =
        true;

struct WireEncodingCompletenessCheck final {
  bool every_kind_encodes = true;
  bool every_kind_named_on_the_wire = true;

  template <typename Component> constexpr void operator()() noexcept {
    every_kind_encodes = every_kind_encodes && kHasWireEncoding<Component>;
    every_kind_named_on_the_wire = every_kind_named_on_the_wire &&
                                   is_v3_component_kind(simulation::component_kind_name<Component>);
  }
};

[[nodiscard]] constexpr WireEncodingCompletenessCheck wire_encoding_completeness() noexcept {
  WireEncodingCompletenessCheck check;
  simulation::ComponentRegistry::for_each_kind(check);
  return check;
}

} // namespace detail

// A kind registered in the world must be publishable, because an entity is its component set and a
// component the wire drops is a world the client renders wrongly rather than partially
// (`docs/protocol/v3.md` § "Versioning and fail-closed decoding").
static_assert(detail::wire_encoding_completeness().every_kind_encodes,
              "every simulation::ComponentRegistry kind must declare a ComponentWireEncoding");

// ...and its declared kind name must be one the accepted v3 schema set names. A kind the schemas do
// not name is a decode failure on every client, so shipping one is a broken wire rather than a
// forward-compatible addition.
static_assert(detail::wire_encoding_completeness().every_kind_named_on_the_wire,
              "every simulation::ComponentRegistry kind name must be a registered v3 component "
              "kind in protocol_v3_constants.hpp and docs/protocol/schema/v3/common.schema.json");

// The wire vocabulary is closed: it names exactly the kinds the world registers and no others, so
// neither side can carry a kind the other has never heard of.
static_assert(simulation::ComponentRegistry::kKindCount == kV3ComponentKindNames.size(),
              "the closed v3 component-kind vocabulary must name exactly the registered kinds");

} // namespace blob_royale::protocol

#endif
