#ifndef BLOB_ROYALE_SIMULATION_TEAM_ID_HPP
#define BLOB_ROYALE_SIMULATION_TEAM_ID_HPP

#include <compare>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: team_id -- the identity of one side in a team mode.
//
// Side membership is optional: an entity without a Team component is unaligned, so there is no
// reserved "no team" value and every TeamId names a real side.
// related: components/team_component.hpp -- the component that carries it.
class TeamId final {
public:
  using Value = std::uint64_t;

  [[nodiscard]] static TeamId create(Value value);

  TeamId(const TeamId&) = default;
  TeamId(TeamId&&) noexcept = default;
  TeamId& operator=(const TeamId&) = default;
  TeamId& operator=(TeamId&&) noexcept = default;
  ~TeamId() = default;

  [[nodiscard]] Value value() const noexcept { return value_; }

  friend bool operator==(const TeamId&, const TeamId&) = default;
  friend std::strong_ordering operator<=>(const TeamId&, const TeamId&) = default;

private:
  explicit TeamId(const Value value) noexcept : value_(value) {}

  Value value_;
};

} // namespace blob_royale::simulation

#endif
