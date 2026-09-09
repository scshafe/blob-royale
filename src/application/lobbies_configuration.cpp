#include "lobbies_configuration.hpp"

#include "application_input_error.hpp"

#include <string>

namespace blob_royale::application {

LobbiesConfiguration LobbiesConfiguration::create(const std::uint64_t count) {
  if (count < kMinimumCount || count > kMaximumCount) {
    throw ApplicationInputError{
        ApplicationInputErrorCode::kLobbiesCountOutOfRange, "lobbies.count",
        "a process runs between " + std::to_string(kMinimumCount) + " and " +
            std::to_string(kMaximumCount) +
            " lobbies, the protocol's directory limit, and this configuration names " +
            std::to_string(count)};
  }
  return LobbiesConfiguration{count};
}

LobbiesConfiguration::LobbiesConfiguration(const std::uint64_t count) noexcept : count_(count) {}

} // namespace blob_royale::application
