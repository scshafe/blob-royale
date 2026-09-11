#ifndef BLOB_ROYALE_APPLICATION_LOBBIES_CONFIGURATION_HPP
#define BLOB_ROYALE_APPLICATION_LOBBIES_CONFIGURATION_HPP

#include "protocol_v3_constants.hpp"

#include <cstdint>

namespace blob_royale::application {

// canonical: lobbies_configuration -- the validated `[lobbies]` section: how many rooms this
// process runs.
//
// One key, `count`, in `[1, kLobbyDirectoryLimit]`. It is a fact about the deployment rather than
// about the match -- every room plays the same `[match]` on the same map -- so it is its own
// section rather than a `[match]` key, and it is bounded by the protocol's directory limit because
// the directory is what publishes the rooms: a process may not run more rooms than it can list
// (`docs/architecture/0006-lobbies-as-rooms.md` § "Rooms"). `1` is the single-match server this
// tree has always been.
// related: application_config.hpp -- the aggregate this is one value of.
// related: ../protocol/protocol_v3_constants.hpp -- `kLobbyDirectoryLimit`, the bound.
class LobbiesConfiguration final {
public:
  static constexpr std::uint64_t kMinimumCount = 1;
  static constexpr std::uint64_t kMaximumCount = protocol::kLobbyDirectoryLimit;

  // Throws ApplicationInputError with `APPLICATION.LOBBIES.COUNT_OUT_OF_RANGE` outside the bound.
  [[nodiscard]] static LobbiesConfiguration create(std::uint64_t count);

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

  friend bool operator==(const LobbiesConfiguration&, const LobbiesConfiguration&) = default;

private:
  explicit LobbiesConfiguration(std::uint64_t count) noexcept;

  std::uint64_t count_;
};

} // namespace blob_royale::application

#endif
