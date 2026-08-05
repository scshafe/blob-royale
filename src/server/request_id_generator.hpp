#ifndef BLOB_ROYALE_SERVER_REQUEST_ID_GENERATOR_HPP
#define BLOB_ROYALE_SERVER_REQUEST_ID_GENERATOR_HPP

#include "request_id.hpp"

#include <cstdint>

namespace blob_royale::server {

// canonical: request_id_generator -- process-opaque collision-resistant v1 correlation IDs.
// The generator is confined to the server event loop; accepted caller IDs bypass it entirely.
class RequestIdGenerator final {
public:
  RequestIdGenerator();

  RequestIdGenerator(const RequestIdGenerator&) = delete;
  RequestIdGenerator(RequestIdGenerator&&) = delete;
  RequestIdGenerator& operator=(const RequestIdGenerator&) = delete;
  RequestIdGenerator& operator=(RequestIdGenerator&&) = delete;
  ~RequestIdGenerator() = default;

  // Produces one schema-valid opaque ID. Throws GameServerError if the sequence is exhausted.
  [[nodiscard]] protocol::RequestId next();

private:
  std::uint64_t process_nonce_;
  std::uint64_t sequence_;
};

} // namespace blob_royale::server

#endif
