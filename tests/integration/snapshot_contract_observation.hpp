#ifndef BLOB_ROYALE_TESTS_INTEGRATION_SNAPSHOT_CONTRACT_OBSERVATION_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_SNAPSHOT_CONTRACT_OBSERVATION_HPP

#include <cstdint>

namespace blob_royale::integration_test {

// Records the ordered scalar observations needed to compare complete snapshot messages.
class SnapshotContractObservation final {
public:
  SnapshotContractObservation(const std::uint64_t tick_sequence,
                              const std::uint64_t message_sequence) noexcept
      : tick_sequence_(tick_sequence), message_sequence_(message_sequence) {}

  [[nodiscard]] std::uint64_t tick_sequence() const noexcept { return tick_sequence_; }
  [[nodiscard]] std::uint64_t message_sequence() const noexcept { return message_sequence_; }

private:
  std::uint64_t tick_sequence_;
  std::uint64_t message_sequence_;
};

} // namespace blob_royale::integration_test

#endif
