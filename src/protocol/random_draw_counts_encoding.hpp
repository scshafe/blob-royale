#ifndef BLOB_ROYALE_PROTOCOL_RANDOM_DRAW_COUNTS_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_RANDOM_DRAW_COUNTS_ENCODING_HPP

#include "component_encoding.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v3_constants.hpp"

#include "random_stream_registry.hpp"

#include <cstdint>
#include <string>

namespace blob_royale::protocol {

// The schema is a closed contract, not a vocabulary inferred silently from a larger registry.
static_assert(simulation::projected_values(simulation::kRandomStreamRegistry,
                                           [](const simulation::RandomStreamDefinition stream) {
                                             return stream.name;
                                           }) == kV3RandomStreamNames,
              "v3 random count keys must exactly match the stream registry's names and order");

// canonical: random_draw_counts_encoding -- the required named count object's production writer.
// Counts retain uint64 precision until this boundary. A value above the wire-safe integer ceiling
// throws RANDOM_DRAW_COUNT_OUT_OF_RANGE with the stream-specific snapshot context; no seed or
// generator state is exposed. The typed input also permits boundary proofs without RNG setters.
// related: docs/protocol/schema/v3/snapshot-data.schema.json -- the closed safe-integer shape.
inline void encode_random_draw_counts(const simulation::RandomDrawCounts& counts,
                                      ComponentObjectSink& sink) {
  for (const simulation::RandomStreamDefinition stream : simulation::kRandomStreamRegistry) {
    const std::uint64_t count = counts[simulation::random_stream_index(stream.kind)];
    if (count > kMaximumSafeInteger) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kRandomDrawCountOutOfRange,
                                  "snapshot_message.data.random_draw_counts." +
                                      std::string(stream.name),
                                  "random draw count must be in the inclusive range 0 to 2^53-1"};
    }
    sink.set_unsigned(stream.name, count);
  }
}

} // namespace blob_royale::protocol

#endif
