#ifndef BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_RULE_NAME_HPP

#include "bounded_name.hpp"
#include "contact_rule_name_policy.hpp"

namespace blob_royale::simulation {

// canonical: contact_rule_name -- the owned, bounded identity of one contact rule row.
//
// This is a **value, not a view**. A ContactEvent carries the name of the row that produced it and
// lives in the tick's event list, while the row itself is owned by a mode's table; a borrowed
// `std::string_view` would therefore be correct only as long as every rule name outlived every
// match, which a mode that builds a row from a temporary `std::string` silently breaks. A dangling
// name surfaces as garbage in a diagnostic rather than as a failure, so the name is copied into
// fixed capacity instead.
//
// Fixed capacity rather than `std::string` because the event list is bounded, tick-local, and
// cleared at commit: an owning allocation per emitted contact would put an allocator on the
// kernel's hot path for a value that is at most kMaximumContactRuleNameLength characters.
//
// The empty default is the unnamed ContactEvent sentinel, never a validated rule. The policy
// retains this domain's grammar and diagnostics; BoundedName owns the shared storage semantics.
// related: contact_rule_name_policy.hpp -- accepted names and structured rejection.
using ContactRuleName = BoundedName<ContactRuleNamePolicy>;

} // namespace blob_royale::simulation

#endif
