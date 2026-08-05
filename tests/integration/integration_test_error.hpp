#ifndef BLOB_ROYALE_TESTS_INTEGRATION_INTEGRATION_TEST_ERROR_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_INTEGRATION_TEST_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {

enum class IntegrationTestErrorCode {
  kArgumentInvalid,
  kFilesystemFailed,
  kProcessFailed,
  kDeadlineExceeded,
  kTransportFailed,
  kContractViolation,
};

// canonical: integration_test_error -- every native integration failure crosses this boundary.
class IntegrationTestError final : public std::runtime_error {
public:
  IntegrationTestError(IntegrationTestErrorCode error_code, std::string operation,
                       std::string context);

  [[nodiscard]] IntegrationTestErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::string_view code() const noexcept;
  [[nodiscard]] const std::string& operation() const& noexcept { return operation_; }
  [[nodiscard]] const std::string& operation() const&& = delete;
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;

private:
  IntegrationTestErrorCode error_code_;
  std::string operation_;
  std::string context_;
};

} // namespace blob_royale::integration_test

#endif
