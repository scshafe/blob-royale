#ifndef BLOB_ROYALE_TESTS_INTEGRATION_LOOPBACK_HTTP_CLIENT_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_LOOPBACK_HTTP_CLIENT_HPP

#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/string_body.hpp>
#include <boost/beast/http/verb.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::integration_test {

using IntegrationHttpResponse = boost::beast::http::response<boost::beast::http::string_body>;
using IntegrationHttpRequest = boost::beast::http::request<boost::beast::http::empty_body>;

// Owns bounded loopback HTTP/1.1 exchanges with fixed transport bounds.
class LoopbackHttpClient final {
public:
  LoopbackHttpClient(std::uint16_t port, std::chrono::milliseconds operation_timeout);

  // `forwarded_client`, when given, is sent as `X-Forwarded-For`: the principal a server that
  // trusts the loopback proxy accounts the request to (`docs/protocol/v3.md` § "Identity").
  [[nodiscard]] IntegrationHttpResponse
  request(boost::beast::http::verb method, std::string_view target, std::string_view request_id,
          std::optional<std::string_view> origin = std::nullopt,
          std::optional<std::string_view> forwarded_client = std::nullopt) const;

  // Writes one to four authored empty-body requests before reading their ordered responses on
  // the same socket. Earlier requests must keep the connection alive. Uses the ordinary request
  // path's deadlines and per-response byte bound; premature upgrade/close fails visibly.
  [[nodiscard]] std::vector<IntegrationHttpResponse>
  request_pipeline(std::span<const IntegrationHttpRequest> requests) const;

  // Completes each empty TCP connection through the server's EOF path before opening the next.
  void exercise_closed_connection_churn(std::size_t connection_count) const;

  // Sends an otherwise-valid request containing raw obs-fold and requires terminal rejection.
  void require_obsolete_line_fold_rejection(std::string_view request_id) const;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  [[nodiscard]] const std::string& host_authority() const& noexcept { return host_authority_; }
  [[nodiscard]] const std::string& host_authority() const&& = delete;

private:
  std::uint16_t port_;
  std::chrono::milliseconds operation_timeout_;
  std::string host_authority_;
};

} // namespace blob_royale::integration_test

#endif
