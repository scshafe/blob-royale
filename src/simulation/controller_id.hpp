#ifndef BLOB_ROYALE_SIMULATION_CONTROLLER_ID_HPP
#define BLOB_ROYALE_SIMULATION_CONTROLLER_ID_HPP

#include <compare>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: controller_id -- the durable identity of one deciding agent.
//
// A controller outlives the entities it drives: an eliminated player that respawns is a new
// EntityId under the same ControllerId, which is what makes score attribution and behavior state
// survive a death without a lookup table. The Controllable component is the only place the two
// identity spaces meet.
// related: entity_id.hpp -- the identity of one body.
class ControllerId final {
public:
  using Value = std::uint64_t;

  [[nodiscard]] static ControllerId create(Value value);

  ControllerId(const ControllerId&) = default;
  ControllerId(ControllerId&&) noexcept = default;
  ControllerId& operator=(const ControllerId&) = default;
  ControllerId& operator=(ControllerId&&) noexcept = default;
  ~ControllerId() = default;

  [[nodiscard]] Value value() const noexcept { return value_; }

  friend bool operator==(const ControllerId&, const ControllerId&) = default;
  friend std::strong_ordering operator<=>(const ControllerId&, const ControllerId&) = default;

private:
  explicit ControllerId(const Value value) noexcept : value_(value) {}

  Value value_;
};

} // namespace blob_royale::simulation

#endif
