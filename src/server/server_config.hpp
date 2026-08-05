#ifndef BLOB_ROYALE_SERVER_SERVER_CONFIG_HPP
#define BLOB_ROYALE_SERVER_SERVER_CONFIG_HPP

#include "public_configuration.hpp"
#include "server_limits.hpp"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::server {

enum class ServerConfigValidationCode {
  kBindAddressInvalid,
  kPortOutOfRange,
  kPresentationRateOutOfRange,
  kPublicConfigurationInvalid,
  kHostAllowlistInvalid,
  kOriginAllowlistInvalid,
  kTrustedProxyAllowlistInvalid,
  kNonLoopbackPolicyInvalid,
};

[[nodiscard]] constexpr std::string_view
server_config_validation_code_name(const ServerConfigValidationCode code) noexcept {
  switch (code) {
  case ServerConfigValidationCode::kBindAddressInvalid:
    return "SERVER.CONFIG.BIND_ADDRESS_INVALID";
  case ServerConfigValidationCode::kPortOutOfRange:
    return "SERVER.CONFIG.PORT_OUT_OF_RANGE";
  case ServerConfigValidationCode::kPresentationRateOutOfRange:
    return "SERVER.CONFIG.PRESENTATION_RATE_OUT_OF_RANGE";
  case ServerConfigValidationCode::kPublicConfigurationInvalid:
    return "SERVER.CONFIG.PUBLIC_CONFIGURATION_INVALID";
  case ServerConfigValidationCode::kHostAllowlistInvalid:
    return "SERVER.CONFIG.HOST_ALLOWLIST_INVALID";
  case ServerConfigValidationCode::kOriginAllowlistInvalid:
    return "SERVER.CONFIG.ORIGIN_ALLOWLIST_INVALID";
  case ServerConfigValidationCode::kTrustedProxyAllowlistInvalid:
    return "SERVER.CONFIG.TRUSTED_PROXY_ALLOWLIST_INVALID";
  case ServerConfigValidationCode::kNonLoopbackPolicyInvalid:
    return "SERVER.CONFIG.NON_LOOPBACK_POLICY_INVALID";
  }
  return "SERVER.CONFIG.VALIDATION_CODE_INVALID";
}

class ServerConfigValidationError final : public std::invalid_argument {
public:
  // Reports a rejected server configuration value with a stable machine code and context.
  ServerConfigValidationError(ServerConfigValidationCode validation_code, std::string context,
                              std::string detail);

  [[nodiscard]] ServerConfigValidationCode validation_code() const noexcept {
    return validation_code_;
  }
  [[nodiscard]] std::string_view code() const noexcept {
    return server_config_validation_code_name(validation_code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(ServerConfigValidationCode validation_code,
                                                 std::string_view context, std::string_view detail);

  ServerConfigValidationCode validation_code_;
  std::string context_;
  std::string detail_;
};

// canonical: server_config -- validated immutable listener, protocol, and trust-boundary policy.
class ServerConfig final {
public:
  static constexpr std::uint64_t kMinimumPort = 1;
  static constexpr std::uint64_t kMaximumPort = 65'535;
  static constexpr std::uint64_t kMinimumSnapshotsPerSecond = 1;
  static constexpr std::uint64_t kMaximumSnapshotsPerSecond = 60;

  // Creates the complete server boundary or throws ServerConfigValidationError.
  // Host and Origin values are canonicalized once here; sessions perform exact comparisons only.
  [[nodiscard]] static ServerConfig create(std::string bind_address, std::uint64_t port,
                                           std::uint64_t snapshots_per_second, double world_width,
                                           double world_height, double player_radius,
                                           std::vector<std::string> allowed_hosts,
                                           std::vector<std::string> allowed_origins,
                                           std::vector<std::string> trusted_proxy_addresses);

  ServerConfig(const ServerConfig&) = default;
  ServerConfig(ServerConfig&&) noexcept = default;
  ServerConfig& operator=(const ServerConfig&) = default;
  ServerConfig& operator=(ServerConfig&&) noexcept = default;
  ~ServerConfig() = default;

  [[nodiscard]] const std::string& bind_address() const& noexcept { return bind_address_; }
  [[nodiscard]] const std::string& bind_address() const&& = delete;
  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] std::uint16_t snapshots_per_second() const noexcept {
    return snapshots_per_second_;
  }
  [[nodiscard]] bool binds_loopback() const noexcept { return binds_loopback_; }
  [[nodiscard]] const protocol::PublicConfiguration& public_configuration() const& noexcept {
    return public_configuration_;
  }
  [[nodiscard]] const protocol::PublicConfiguration& public_configuration() const&& = delete;
  [[nodiscard]] std::span<const std::string> allowed_hosts() const& noexcept {
    return allowed_hosts_;
  }
  [[nodiscard]] std::span<const std::string> allowed_hosts() const&& = delete;
  [[nodiscard]] std::span<const std::string> allowed_origins() const& noexcept {
    return allowed_origins_;
  }
  [[nodiscard]] std::span<const std::string> allowed_origins() const&& = delete;
  [[nodiscard]] std::span<const std::string> trusted_proxy_addresses() const& noexcept {
    return trusted_proxy_addresses_;
  }
  [[nodiscard]] std::span<const std::string> trusted_proxy_addresses() const&& = delete;

  [[nodiscard]] bool allows_host(std::string_view normalized_authority) const noexcept;
  [[nodiscard]] bool allows_origin(std::string_view normalized_origin) const noexcept;
  [[nodiscard]] bool trusts_proxy_address(std::string_view canonical_address) const noexcept;

  friend bool operator==(const ServerConfig&, const ServerConfig&) = default;

private:
  ServerConfig(std::string bind_address, std::uint16_t port, std::uint16_t snapshots_per_second,
               protocol::PublicConfiguration public_configuration,
               std::vector<std::string> allowed_hosts, std::vector<std::string> allowed_origins,
               std::vector<std::string> trusted_proxy_addresses, bool binds_loopback) noexcept;

  std::string bind_address_;
  std::uint16_t port_;
  std::uint16_t snapshots_per_second_;
  protocol::PublicConfiguration public_configuration_;
  std::vector<std::string> allowed_hosts_;
  std::vector<std::string> allowed_origins_;
  std::vector<std::string> trusted_proxy_addresses_;
  bool binds_loopback_;
};

// Normalizes one untrusted Host authority for an exact ServerConfig comparison.
// Returns an empty string for invalid syntax; it never reflects the value in an error response.
[[nodiscard]] std::string normalize_host_authority(std::string_view authority,
                                                   std::uint16_t default_port);

// Normalizes one untrusted serialized HTTP Origin for an exact ServerConfig comparison.
// Returns an empty string for invalid syntax, including null, paths, credentials, or fragments.
[[nodiscard]] std::string normalize_serialized_origin(std::string_view origin);

} // namespace blob_royale::server

#endif
