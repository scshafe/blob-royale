#include "server_config.hpp"

#include "protocol_encoding_error.hpp"

#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace blob_royale::server {
namespace {

using Address = boost::asio::ip::address;

[[nodiscard]] bool is_ascii_digit(const char character) noexcept {
  return character >= '0' && character <= '9';
}

[[nodiscard]] bool is_ascii_alpha(const char character) noexcept {
  return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
}

[[nodiscard]] char to_ascii_lower(const char character) noexcept {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character + ('a' - 'A'));
  }
  return character;
}

[[nodiscard]] std::optional<Address> parse_canonical_address(const std::string_view text) {
  if (text.empty() || text.find('%') != std::string_view::npos) {
    return std::nullopt;
  }
  boost::system::error_code parse_error;
  const Address address = boost::asio::ip::make_address(text, parse_error);
  if (parse_error || address.to_string() != text) {
    return std::nullopt;
  }
  return address;
}

[[nodiscard]] bool is_private_ipv4(const boost::asio::ip::address_v4 address) noexcept {
  const auto bytes = address.to_bytes();
  return bytes[0] == 10U || (bytes[0] == 172U && bytes[1] >= 16U && bytes[1] <= 31U) ||
         (bytes[0] == 192U && bytes[1] == 168U);
}

[[nodiscard]] bool is_private_ipv6(const boost::asio::ip::address_v6 address) noexcept {
  const auto bytes = address.to_bytes();
  return (bytes[0] & 0xFEU) == 0xFCU;
}

[[nodiscard]] bool is_supported_bind_address(const Address& address) noexcept {
  if (address.is_loopback() || address.is_unspecified()) {
    return true;
  }
  return address.is_v4() ? is_private_ipv4(address.to_v4()) : is_private_ipv6(address.to_v6());
}

[[nodiscard]] bool is_valid_dns_name(const std::string_view host) noexcept {
  if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') {
    return false;
  }
  std::size_t label_length = 0;
  bool label_starts_with_hyphen = false;
  char previous = '\0';
  for (const char character : host) {
    if (character == '.') {
      if (label_length == 0 || label_length > 63 || label_starts_with_hyphen || previous == '-') {
        return false;
      }
      label_length = 0;
      label_starts_with_hyphen = false;
      previous = character;
      continue;
    }
    if (!is_ascii_alpha(character) && !is_ascii_digit(character) && character != '-') {
      return false;
    }
    if (label_length == 0) {
      label_starts_with_hyphen = character == '-';
    }
    ++label_length;
    previous = character;
  }
  return label_length > 0 && label_length <= 63 && !label_starts_with_hyphen && previous != '-';
}

[[nodiscard]] std::optional<std::string>
normalize_port_suffix(const std::string_view suffix,
                      const std::optional<std::uint16_t> default_port) {
  if (suffix.empty()) {
    if (!default_port.has_value()) {
      return std::string{};
    }
    return std::string{":"}.append(std::to_string(*default_port));
  }
  if (suffix.front() != ':' || suffix.size() == 1 || (suffix.size() > 2 && suffix[1] == '0')) {
    return std::nullopt;
  }
  std::uint32_t port = 0;
  for (std::size_t index = 1; index < suffix.size(); ++index) {
    if (!is_ascii_digit(suffix[index])) {
      return std::nullopt;
    }
    port = (port * 10U) + static_cast<std::uint32_t>(suffix[index] - '0');
    if (port > ServerConfig::kMaximumPort) {
      return std::nullopt;
    }
  }
  if (port < ServerConfig::kMinimumPort) {
    return std::nullopt;
  }
  return std::string{suffix};
}

[[nodiscard]] std::string normalize_authority(const std::string_view authority,
                                              const std::optional<std::uint16_t> default_port) {
  if (authority.empty() ||
      authority.size() > ServerLimits::kConfigurationAllowlistEntryMaximumByteCount ||
      authority.find_first_of("@/?# \t\r\n") != std::string_view::npos) {
    return {};
  }

  if (authority.front() == '[') {
    const std::size_t bracket = authority.find(']');
    if (bracket == std::string_view::npos) {
      return {};
    }
    const std::string_view address_text = authority.substr(1, bracket - 1);
    const auto address = parse_canonical_address(address_text);
    if (!address.has_value() || !address->is_v6()) {
      return {};
    }
    const auto port = normalize_port_suffix(authority.substr(bracket + 1), default_port);
    if (!port.has_value()) {
      return {};
    }
    std::string normalized{"["};
    normalized.append(address_text);
    normalized.push_back(']');
    normalized.append(*port);
    return normalized;
  }

  const std::size_t colon = authority.find(':');
  if (colon != std::string_view::npos && authority.find(':', colon + 1) != std::string_view::npos) {
    return {};
  }
  const std::string_view host = authority.substr(0, colon);
  const std::string_view suffix =
      colon == std::string_view::npos ? std::string_view{} : authority.substr(colon);
  const auto port = normalize_port_suffix(suffix, default_port);
  if (!port.has_value() || host.empty()) {
    return {};
  }

  if (const auto address = parse_canonical_address(host); address.has_value()) {
    if (!address->is_v4()) {
      return {};
    }
    std::string normalized{host};
    normalized.append(*port);
    return normalized;
  }

  std::string normalized_host;
  normalized_host.reserve(host.size());
  for (const char character : host) {
    normalized_host.push_back(to_ascii_lower(character));
  }
  if (!is_valid_dns_name(normalized_host)) {
    return {};
  }
  normalized_host.append(*port);
  return normalized_host;
}

template <typename Normalizer>
[[nodiscard]] std::vector<std::string>
normalize_allowlist(std::vector<std::string> entries, const ServerConfigValidationCode error_code,
                    const std::string_view context, const bool requires_nonempty,
                    Normalizer&& normalize) {
  if ((requires_nonempty && entries.empty()) ||
      entries.size() > ServerLimits::kConfigurationAllowlistMaximumEntryCount) {
    throw ServerConfigValidationError{error_code, std::string{context},
                                      "allowlist entry count is outside the accepted bound"};
  }

  std::size_t aggregate_size = 0;
  for (std::string& entry : entries) {
    if (entry.empty() ||
        entry.size() > ServerLimits::kConfigurationAllowlistEntryMaximumByteCount) {
      throw ServerConfigValidationError{error_code, std::string{context},
                                        "an allowlist entry has an invalid byte length"};
    }
    std::string normalized = normalize(entry);
    if (normalized.empty()) {
      throw ServerConfigValidationError{
          error_code, std::string{context},
          "an allowlist entry is not canonical or has invalid syntax"};
    }
    aggregate_size += normalized.size();
    if (aggregate_size > ServerLimits::kConfigurationAllowlistMaximumAggregateByteCount) {
      throw ServerConfigValidationError{error_code, std::string{context},
                                        "aggregate allowlist bytes exceed the accepted bound"};
    }
    entry = std::move(normalized);
  }

  std::ranges::sort(entries);
  if (std::adjacent_find(entries.begin(), entries.end()) != entries.end()) {
    throw ServerConfigValidationError{error_code, std::string{context},
                                      "allowlist contains a duplicate canonical entry"};
  }
  return entries;
}

[[nodiscard]] std::string normalize_proxy_address(const std::string_view address) {
  return parse_canonical_address(address).has_value() ? std::string{address} : std::string{};
}

template <typename Range>
[[nodiscard]] bool contains_exact(const Range& values, const std::string_view candidate) noexcept {
  const auto entry = std::lower_bound(
      values.begin(), values.end(), candidate,
      [](const std::string& value, const std::string_view searched) { return value < searched; });
  return entry != values.end() && *entry == candidate;
}

} // namespace

ServerConfigValidationError::ServerConfigValidationError(
    const ServerConfigValidationCode validation_code, std::string context, std::string detail)
    : std::invalid_argument(build_message(validation_code, context, detail)),
      validation_code_(validation_code), context_(std::move(context)), detail_(std::move(detail)) {}

std::string
ServerConfigValidationError::build_message(const ServerConfigValidationCode validation_code,
                                           const std::string_view context,
                                           const std::string_view detail) {
  std::string message;
  message.reserve(server_config_validation_code_name(validation_code).size() + context.size() +
                  detail.size() + 4);
  message.append(server_config_validation_code_name(validation_code));
  message.append(" [");
  message.append(context);
  message.append("]: ");
  message.append(detail);
  return message;
}

ServerConfig ServerConfig::create(std::string bind_address, const std::uint64_t port,
                                  const std::uint64_t snapshots_per_second,
                                  const double world_width, const double world_height,
                                  const double player_radius,
                                  std::vector<std::string> allowed_hosts,
                                  std::vector<std::string> allowed_origins,
                                  std::vector<std::string> trusted_proxy_addresses) {
  const std::optional<Address> parsed_bind_address = parse_canonical_address(bind_address);
  if (!parsed_bind_address.has_value() || !is_supported_bind_address(*parsed_bind_address)) {
    throw ServerConfigValidationError{
        ServerConfigValidationCode::kBindAddressInvalid, "server.bind_address",
        "value must be a canonical loopback, unspecified, or private numeric address"};
  }
  if (port < kMinimumPort || port > kMaximumPort) {
    throw ServerConfigValidationError{ServerConfigValidationCode::kPortOutOfRange, "server.port",
                                      "value must be between 1 and 65535"};
  }
  if (snapshots_per_second < kMinimumSnapshotsPerSecond ||
      snapshots_per_second > kMaximumSnapshotsPerSecond) {
    throw ServerConfigValidationError{ServerConfigValidationCode::kPresentationRateOutOfRange,
                                      "presentation.snapshots_per_second",
                                      "value must be between 1 and 60"};
  }

  protocol::PublicConfiguration public_configuration = [&] {
    try {
      return protocol::PublicConfiguration::create(world_width, world_height, player_radius,
                                                   snapshots_per_second);
    } catch (const protocol::ProtocolEncodingError& error) {
      throw ServerConfigValidationError{ServerConfigValidationCode::kPublicConfigurationInvalid,
                                        error.context(), error.detail()};
    }
  }();

  allowed_hosts = normalize_allowlist(
      std::move(allowed_hosts), ServerConfigValidationCode::kHostAllowlistInvalid,
      "server.allowed_hosts", true, [port](const std::string_view authority) {
        return normalize_authority(authority, static_cast<std::uint16_t>(port));
      });
  allowed_origins = normalize_allowlist(
      std::move(allowed_origins), ServerConfigValidationCode::kOriginAllowlistInvalid,
      "server.allowed_origins", false, normalize_serialized_origin);
  trusted_proxy_addresses = normalize_allowlist(
      std::move(trusted_proxy_addresses), ServerConfigValidationCode::kTrustedProxyAllowlistInvalid,
      "server.trusted_proxy_addresses", false, normalize_proxy_address);

  const bool binds_loopback = parsed_bind_address->is_loopback();
  if (!binds_loopback && (allowed_origins.empty() || trusted_proxy_addresses.empty())) {
    throw ServerConfigValidationError{
        ServerConfigValidationCode::kNonLoopbackPolicyInvalid, "server.bind_address",
        "non-loopback binding requires nonempty Origin and trusted-proxy allowlists"};
  }

  return ServerConfig{std::move(bind_address),
                      static_cast<std::uint16_t>(port),
                      static_cast<std::uint16_t>(snapshots_per_second),
                      std::move(public_configuration),
                      std::move(allowed_hosts),
                      std::move(allowed_origins),
                      std::move(trusted_proxy_addresses),
                      binds_loopback};
}

ServerConfig::ServerConfig(std::string bind_address, const std::uint16_t port,
                           const std::uint16_t snapshots_per_second,
                           protocol::PublicConfiguration public_configuration,
                           std::vector<std::string> allowed_hosts,
                           std::vector<std::string> allowed_origins,
                           std::vector<std::string> trusted_proxy_addresses,
                           const bool binds_loopback) noexcept
    : bind_address_(std::move(bind_address)), port_(port),
      snapshots_per_second_(snapshots_per_second),
      public_configuration_(std::move(public_configuration)),
      allowed_hosts_(std::move(allowed_hosts)), allowed_origins_(std::move(allowed_origins)),
      trusted_proxy_addresses_(std::move(trusted_proxy_addresses)),
      binds_loopback_(binds_loopback) {}

bool ServerConfig::allows_host(const std::string_view normalized_authority) const noexcept {
  return contains_exact(allowed_hosts_, normalized_authority);
}

bool ServerConfig::allows_origin(const std::string_view normalized_origin) const noexcept {
  return contains_exact(allowed_origins_, normalized_origin);
}

bool ServerConfig::trusts_proxy_address(const std::string_view canonical_address) const noexcept {
  return contains_exact(trusted_proxy_addresses_, canonical_address);
}

std::string normalize_host_authority(const std::string_view authority,
                                     const std::uint16_t default_port) {
  return normalize_authority(authority, default_port);
}

std::string normalize_serialized_origin(const std::string_view origin) {
  if (origin.size() > ServerLimits::kConfigurationAllowlistEntryMaximumByteCount ||
      origin == "null") {
    return {};
  }

  std::size_t authority_offset = 0;
  std::string scheme;
  if (origin.size() >= 7) {
    std::string prefix;
    prefix.reserve(8);
    const std::size_t prefix_size = std::min<std::size_t>(origin.size(), 8);
    for (std::size_t index = 0; index < prefix_size; ++index) {
      prefix.push_back(to_ascii_lower(origin[index]));
    }
    if (prefix.starts_with("http://")) {
      scheme = "http://";
      authority_offset = 7;
    } else if (prefix.starts_with("https://")) {
      scheme = "https://";
      authority_offset = 8;
    }
  }
  if (scheme.empty() || authority_offset >= origin.size()) {
    return {};
  }

  const std::uint16_t default_port = scheme == "http://" ? 80 : 443;
  const std::string normalized_authority =
      normalize_authority(origin.substr(authority_offset), default_port);
  if (normalized_authority.empty()) {
    return {};
  }
  scheme.append(normalized_authority);
  return scheme;
}

} // namespace blob_royale::server
