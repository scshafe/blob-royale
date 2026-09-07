#ifndef BLOB_ROYALE_SIMULATION_CONTACT_RULE_TABLE_HPP
#define BLOB_ROYALE_SIMULATION_CONTACT_RULE_TABLE_HPP

#include "contact_rule.hpp"
#include "entity_id.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

class GameWorld;

// canonical: contact_orientation -- which way round a matched row read its canonical pair.
//
// Canonical means the row's first predicate matched the lower EntityId. Swapped means it matched
// the higher one, and the kernel presents the row's arguments in row orientation and maps the
// returned bodies back to the canonical pair.
enum class ContactOrientation : std::uint8_t {
  kCanonical = 0,
  kSwapped = 1,
};

[[nodiscard]] constexpr std::string_view
contact_orientation_name(const ContactOrientation orientation) noexcept {
  switch (orientation) {
  case ContactOrientation::kCanonical:
    return "canonical";
  case ContactOrientation::kSwapped:
    return "swapped";
  }
  return "contact_orientation_invalid";
}

// canonical: contact_rule_table -- the ordered chain of responsibility for one mode.
//
// Row order is the declared precedence, so **precedence is a property of the mode's written list
// and never of registration, allocation, or static-initialization order**
// (`docs/architecture/0004-gameplay-architecture.md` § "Contact rules").
//
// The mode returns the whole table. The engine never appends a row a mode did not list, so a mode
// that wants the defaults writes them into its own declared order and the precedence between mode
// rows and built-in rows is visible in the mode's source rather than hidden in engine composition.
// related: contact_rule.hpp -- the row this table orders.
// related: game_simulation.hpp -- the kernel phase that walks it once per admitted contact.
class ContactRuleTable final {
public:
  // One matched (row, orientation), which is the whole result of walking the chain.
  struct Match final {
    std::size_t row_index{};
    ContactOrientation orientation{};

    friend bool operator==(const Match&, const Match&) = default;
  };

  // Rejects a null predicate or response through ContactRule::create, and rejects a duplicate row
  // name here so a failure names one row. Row order is preserved exactly as declared.
  [[nodiscard]] static ContactRuleTable create(std::vector<ContactRule> rows);

  // The two rows ADR 0003 § "Canonical tick" requires, in this order: `elastic_disc` for a
  // dynamic-dynamic pair, then `reflect_static` for a dynamic body meeting a static one. They are
  // the accepted baseline, which is a requirement on them rather than a description of them: a
  // table whose built-in rows disagree with ADR 0003 § "Player-pair policy" or § "Wall policy" is
  // a defect in the table, not a new physics policy.
  [[nodiscard]] static ContactRuleTable built_in();

  // The table of a mode that declares no interaction at all. Every admitted contact then matches
  // no row and is unchanged, which is the totality rule stated without a default row.
  [[nodiscard]] static ContactRuleTable empty();

  ContactRuleTable(const ContactRuleTable&) = default;
  ContactRuleTable(ContactRuleTable&&) noexcept = default;
  ContactRuleTable& operator=(const ContactRuleTable&) = default;
  ContactRuleTable& operator=(ContactRuleTable&&) noexcept = default;
  ~ContactRuleTable() = default;

  [[nodiscard]] std::span<const ContactRule> rows() const& noexcept { return rows_; }
  [[nodiscard]] std::span<const ContactRule> rows() const&& = delete;

  [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

  // Walks `rows()` in declared order and returns the first matching (row, orientation), trying the
  // canonical orientation before the swapped one within each row. Nullopt when no row matches,
  // which leaves the pair unchanged.
  [[nodiscard]] std::optional<Match> first_match(const GameWorld& world, EntityId canonical_first,
                                                 EntityId canonical_second) const;

  friend bool operator==(const ContactRuleTable&, const ContactRuleTable&) = default;

private:
  explicit ContactRuleTable(std::vector<ContactRule> rows) noexcept;

  std::vector<ContactRule> rows_;
};

// The declared names of the two built-in rows, so a consuming system matches a ContactEvent's
// `rule_name` against one identity rather than a repeated literal.
inline constexpr std::string_view kElasticDiscContactRuleName = "elastic_disc";
inline constexpr std::string_view kReflectStaticContactRuleName = "reflect_static";

// The two built-in predicates, published because a mode's own row composes them: a `flag_pickup`
// row is `(dynamic, dynamic)` with a different response, and a `power_up_pickup` row is
// `(dynamic, static)`.
//
// Both are total: an entity carrying no PhysicsBody satisfies neither, so a pair the broad phase
// could not have produced still resolves to "no row matched" rather than to a lookup failure.
[[nodiscard]] bool body_is_dynamic(const GameWorld& world, EntityId entity);
[[nodiscard]] bool body_is_static(const GameWorld& world, EntityId entity);

// ADR 0003 § "Player-pair policy" applied to two dynamic discs: the equal-mass frictionless
// exchange of normal velocity components, delegated verbatim to
// `resolve_player_pair_collision`, plus one ContactEvent. Published so a mode can reuse the
// accepted equation under its own row name.
[[nodiscard]] ContactResponse elastic_disc_response(const ContactRule::Subject& first,
                                                    const ContactRule::Subject& second,
                                                    const PlayerPairContact& contact,
                                                    const TickContext& context);

// ADR 0003 § "Wall policy" applied to a body instead of an arena edge: reflect the dynamic body's
// normal component about the contact normal, leave its tangential component attached to it, and
// leave the static body untouched. `first` is the dynamic body and `second` the static one.
[[nodiscard]] ContactResponse reflect_static_response(const ContactRule::Subject& first,
                                                      const ContactRule::Subject& second,
                                                      const PlayerPairContact& contact,
                                                      const TickContext& context);

} // namespace blob_royale::simulation

#endif
