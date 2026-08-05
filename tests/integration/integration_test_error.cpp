#include "integration_test_error.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::integration_test {
namespace {

constexpr std::array<std::string_view, 6> kIntegrationTestErrorCodes = {
    "INTEGRATION.ARGUMENT_INVALID", "INTEGRATION.FILESYSTEM_FAILED",
    "INTEGRATION.PROCESS_FAILED",   "INTEGRATION.DEADLINE_EXCEEDED",
    "INTEGRATION.TRANSPORT_FAILED", "INTEGRATION.CONTRACT_VIOLATION",
};

[[nodiscard]] std::string compose_message(const std::string_view operation,
                                          const std::string_view context) {
  std::string message{operation};
  message.append(": ");
  message.append(context);
  return message;
}

} // namespace

IntegrationTestError::IntegrationTestError(const IntegrationTestErrorCode error_code,
                                           std::string operation, std::string context)
    : std::runtime_error(compose_message(operation, context)), error_code_(error_code),
      operation_(std::move(operation)), context_(std::move(context)) {}

std::string_view IntegrationTestError::code() const noexcept {
  return kIntegrationTestErrorCodes[static_cast<std::size_t>(error_code_)];
}

} // namespace blob_royale::integration_test
