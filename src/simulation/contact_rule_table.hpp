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
  //
  // **`variable_impulse` is declared above both, and that ordering is what makes it an addition
  // rather than an amendment.** `first_match` takes the first matching row in declared order, and
  // `variable_impulse` matches only when one of the two bodies differs from the baseline in mass or
  // restitution. A world of ordinary blobs therefore never reaches it: every pair falls through to
  // `elastic_disc`, which still calls `resolve_player_pair_collision` unmodified, so every accepted
  // fixture horizon and the accepted-baseline oracle keep their exact arithmetic. Declaring it
  // *below* `elastic_disc` would make it unreachable instead, because `elastic_disc` matches every
  // dynamic pair including a variable one.
  [[nodiscard]] static ContactRuleTable built_in();

  // canonical: rows_above_built_in -- "these rows, then the built-in ones", as one call.
  //
  // The engine still never appends a row a mode did not ask for: a mode that wants the defaults
  // must say so, and this is how it says so. What it removes is only the transcription. Before it
  // existed, a mode adding one row of its own had to rebuild all three built-in rows by hand, which
  // put a second copy of the accepted baseline's predicates and responses in that mode's source --
  // free to drift from `built_in()` without one test noticing, which is exactly the defect the
  // published `elastic_disc_response` and friends exist to prevent.
  //
  // **Precedence stays visible in the mode's own source**, which is
  // `docs/architecture/0004-gameplay-architecture.md` § "Contact rules"'s actual requirement: the
  // caller writes its rows in the order it wants them and the name of this function says where the
  // built-in ones land relative to them. A mode that needs a row *below* the built-in rows, or
  // between two of them, does not use this and writes the whole list; that is a deliberate
  // asymmetry, because "above everything the engine ships" is the only position a mode has so far
  // wanted and a general splice would be a seam with no second caller.
  //
  // Duplicate names are rejected exactly as `create` rejects them, so a mode that redeclares
  // `elastic_disc` is a startup failure naming the row rather than a silently shadowed baseline.
  [[nodiscard]] static ContactRuleTable with_rows_above_built_in(std::vector<ContactRule> rows);

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

// The declared names of the built-in rows, so a consuming system matches a ContactEvent's
// `rule_name` against one identity rather than a repeated literal.
inline constexpr std::string_view kVariableImpulseContactRuleName = "variable_impulse";
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

// The `variable_impulse` first predicate: a dynamic body that is **not** the baseline body, that
// is, one whose mass or restitution differs from `PhysicsBody`'s defaults. Paired with
// `body_is_dynamic` as the second predicate it matches a dynamic pair in which at least one body is
// variable, in either orientation, and never a pair of ordinary blobs.
//
// Mass and restitution are legal for a predicate to read for the same reason the static flag is:
// no phase writes either within a tick, so the committed value this reads is the value the response
// will use.
//
// Total in the same way as the two above: an entity carrying no PhysicsBody does not satisfy it.
[[nodiscard]] bool body_is_variable_dynamic(const GameWorld& world, EntityId entity);

// ADR 0003 § "Player-pair policy" applied to two dynamic discs: the equal-mass frictionless
// exchange of normal velocity components, delegated verbatim to
// `resolve_player_pair_collision`, plus one ContactEvent. Published so a mode can reuse the
// accepted equation under its own row name.
[[nodiscard]] ContactResponse elastic_disc_response(const ContactRule::Subject& first,
                                                    const ContactRule::Subject& second,
                                                    const PlayerPairContact& contact,
                                                    const TickContext& context);

// The general impulse of `resolve_general_pair_collision` applied to two dynamic discs, plus one
// ContactEvent. This is the same shape as `elastic_disc_response` and delegates the equation the
// same way; what differs is only which pure function it selects. Published so a mode can reuse the
// general equation under its own row name.
[[nodiscard]] ContactResponse variable_impulse_response(const ContactRule::Subject& first,
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
