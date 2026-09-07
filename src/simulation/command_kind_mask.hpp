#ifndef BLOB_ROYALE_SIMULATION_COMMAND_KIND_MASK_HPP
#define BLOB_ROYALE_SIMULATION_COMMAND_KIND_MASK_HPP

#include "command_registry.hpp"
#include "simulation_validation_error.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>

namespace blob_royale::simulation {

// canonical: command_kind_mask -- the set of kinds a mode accepts.
//
// One integer over the CommandKind bits, so the mode's accepted set is copied by value into the
// runtime at construction, carried in the protocol v2 `welcome` message, and checked at the
// boundary and again in InputBatch::create without allocating or ordering anything.
//
// The value is immutable: `with` returns a new mask rather than mutating, so an accepted set that
// has been handed to the runtime cannot widen behind its holder's back.
// related: command_registry.hpp -- the closed list of kinds this is a set over.
// related: input_batch.hpp -- where the accepted set is enforced.
class CommandKindMask final {
public:
  using Bits = std::uint32_t;

  // Creates a mask from raw bits. Throws SimulationValidationError when a bit belongs to no
  // registered kind, so an unknown wire value cannot become a silently empty or wider set.
  [[nodiscard]] static CommandKindMask create(const Bits bits) {
    if ((bits & all_bits()) != bits) {
      throw SimulationValidationError(SimulationValidationCode::kCommandKindMaskUnknownBit,
                                      "command_kind_mask.bits",
                                      "mask bits " + std::to_string(bits) +
                                          " include a bit belonging to no registered command kind");
    }
    return CommandKindMask(bits);
  }

  // Creates a mask naming its kinds, which is how a mode declares its accepted set.
  [[nodiscard]] static CommandKindMask create(const std::initializer_list<CommandKind> kinds) {
    Bits bits = 0;
    for (const CommandKind kind : kinds) {
      bits |= static_cast<Bits>(kind);
    }
    return CommandKindMask(bits);
  }

  // The empty set: a mode that accepts no command at all.
  [[nodiscard]] static CommandKindMask none() noexcept { return CommandKindMask(0); }

  // Every registered kind, generated from kCommandKinds so it cannot omit a newly added kind.
  [[nodiscard]] static CommandKindMask all() noexcept { return CommandKindMask(all_bits()); }

  CommandKindMask(const CommandKindMask&) = default;
  CommandKindMask(CommandKindMask&&) noexcept = default;
  CommandKindMask& operator=(const CommandKindMask&) = default;
  CommandKindMask& operator=(CommandKindMask&&) noexcept = default;
  ~CommandKindMask() = default;

  [[nodiscard]] bool contains(const CommandKind kind) const noexcept {
    return (bits_ & static_cast<Bits>(kind)) != 0;
  }

  // Immutable insert: the mask with this kind added, leaving this value unchanged.
  [[nodiscard]] CommandKindMask with(const CommandKind kind) const noexcept {
    return CommandKindMask(bits_ | static_cast<Bits>(kind));
  }

  [[nodiscard]] bool empty() const noexcept { return bits_ == 0; }

  // The raw bits, for the protocol boundary that publishes the accepted set.
  [[nodiscard]] Bits bits() const noexcept { return bits_; }

  friend bool operator==(const CommandKindMask&, const CommandKindMask&) = default;

private:
  explicit constexpr CommandKindMask(const Bits bits) noexcept : bits_(bits) {}

  [[nodiscard]] static constexpr Bits all_bits() noexcept {
    Bits bits = 0;
    for (const CommandKind kind : kCommandKinds) {
      bits |= static_cast<Bits>(kind);
    }
    return bits;
  }

  Bits bits_;
};

} // namespace blob_royale::simulation

#endif
