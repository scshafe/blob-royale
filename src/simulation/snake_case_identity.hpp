#ifndef BLOB_ROYALE_SIMULATION_SNAKE_CASE_IDENTITY_HPP
#define BLOB_ROYALE_SIMULATION_SNAKE_CASE_IDENTITY_HPP

#include "simulation_limits.hpp"

#include <cstddef>
#include <string_view>

namespace blob_royale::simulation {

// canonical: snake_case_identity -- the one grammar every named kind in this system must satisfy.
//
// A lower-case letter, then any number of lower-case letters, digits, and underscores. That is the
// vocabulary contact rule names, component kind names, map marker kinds, mode names, controller
// kinds, and hazard kinds all already share, and `docs/protocol/schema/v2/common.schema.json`
// § `$defs/kind_name` is the same rule stated as a pattern for the wire.
//
// **It lives in `blob_simulation` because that is the only library all of its callers can reach.**
// `blob_gameplay`, `blob_application`, and `blob_protocol` each depend on `blob_simulation` and
// none may depend on another, so a grammar written in any one of them is a grammar the others must
// copy -- which is exactly what happened: this predicate existed three times, in
// `contact_rule_name.hpp`, `application/match_configuration.cpp`, and
// `gameplay/shared/hazard_archetype.cpp`, and the three could have drifted apart without one test
// noticing. A name that one layer accepts and another rejects is a startup failure at best and an
// unencodable frame at worst.
//
// Length is deliberately **not** part of this predicate. Each caller bounds its own storage --
// `ContactRuleName` by its fixed capacity, a wire kind name by the published schema -- and folding
// one caller's bound in here would silently impose it on the others.
// related: simulation_limits.hpp -- the bounds callers apply alongside this grammar.
// related: contact_rule_name.hpp -- a validated owned name built on this.
[[nodiscard]] constexpr bool is_snake_case_identity(const std::string_view value) noexcept {
  // Total over every input, including the empty string, so a caller may check emptiness separately
  // for a better message without this predicate depending on it having done so.
  if (value.empty() || value.front() < 'a' || value.front() > 'z') {
    return false;
  }
  for (const char character : value) {
    const bool accepted = (character >= 'a' && character <= 'z') ||
                          (character >= '0' && character <= '9') || character == '_';
    if (!accepted) {
      return false;
    }
  }
  return true;
}

// `common.schema.json#/$defs/kind_name`: the snake_case grammar within the published kind-name
// length. A configured name that fails this could never be encoded, so every layer that accepts a
// kind name from configuration rejects it at startup rather than emitting a frame each client must
// close on.
[[nodiscard]] constexpr bool is_wire_kind_name(const std::string_view value) noexcept {
  return value.size() <= kMaximumKindNameLength && is_snake_case_identity(value);
}

} // namespace blob_royale::simulation

#endif
