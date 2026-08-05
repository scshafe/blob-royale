#ifndef BLOB_ROYALE_SIMULATION_ENTITY_ID_HPP
#define BLOB_ROYALE_SIMULATION_ENTITY_ID_HPP

#include <compare>
#include <cstdint>

namespace blob_royale::simulation {

class EntityId final {
public:
  using Value = std::uint64_t;

  [[nodiscard]] static EntityId create(Value value);

  EntityId(const EntityId&) = default;
  EntityId(EntityId&&) noexcept = default;
  EntityId& operator=(const EntityId&) = default;
  EntityId& operator=(EntityId&&) noexcept = default;
  ~EntityId() = default;

  [[nodiscard]] Value value() const noexcept { return value_; }

  friend bool operator==(const EntityId&, const EntityId&) = default;
  friend std::strong_ordering operator<=>(const EntityId&, const EntityId&) = default;

private:
  explicit EntityId(const Value value) noexcept : value_(value) {}

  Value value_;
};

} // namespace blob_royale::simulation

#endif
