#include "random_draw_counts_encoding.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace protocol = blob_royale::protocol;
namespace simulation = blob_royale::simulation;

namespace {

// Captures only unsigned count members. Any other sink operation is a writer defect, not a
// coercion that the boundary proof should accept. No generator state can be injected by this test.
class RandomDrawCountSink final : public protocol::ComponentObjectSink {
public:
  std::vector<std::pair<std::string, std::uint64_t>> members;

  void set_unsigned(const std::string_view name, const std::uint64_t value) override {
    members.emplace_back(name, value);
  }
  void set_number(std::string_view, double) override { unexpected_member(); }
  void set_signed(std::string_view, std::int64_t) override { unexpected_member(); }
  void set_boolean(std::string_view, bool) override { unexpected_member(); }
  void set_string(std::string_view, std::string_view) override { unexpected_member(); }
  void set_vector(std::string_view, double, double) override { unexpected_member(); }
  void set_object_array(
      std::string_view, std::size_t,
      const std::function<void(std::size_t, protocol::ComponentObjectSink&)>&) override {
    unexpected_member();
  }

private:
  [[noreturn]] static void unexpected_member() {
    throw std::logic_error("TEST.RANDOM_DRAW_COUNT_NONUNSIGNED_MEMBER");
  }
};

} // namespace

TEST_CASE("random count writer publishes zero and exact safe ceilings in registry order",
          "[unit][protocol][v3][random_draw_counts][boundary]") {
  for (const std::uint64_t hazards : {std::uint64_t{0}, protocol::kMaximumSafeInteger}) {
    for (const std::uint64_t hill : {std::uint64_t{0}, protocol::kMaximumSafeInteger}) {
      const simulation::RandomDrawCounts counts{hazards, hill};
      RandomDrawCountSink sink;
      protocol::encode_random_draw_counts(counts, sink);
      CHECK(sink.members == std::vector<std::pair<std::string, std::uint64_t>>{
                                {"hazards", hazards}, {"hill", hill}});
    }
  }
}

TEST_CASE("random count writer rejects unsafe uint64 values with the exact stream context",
          "[unit][protocol][v3][random_draw_counts][rejection]") {
  for (const simulation::RandomStreamDefinition stream : simulation::kRandomStreamRegistry) {
    for (const std::uint64_t invalid : {protocol::kMaximumSafeInteger + 1,
                                       std::numeric_limits<std::uint64_t>::max()}) {
      simulation::RandomDrawCounts counts{};
      counts[simulation::random_stream_index(stream.kind)] = invalid;
      RandomDrawCountSink sink;
      try {
        protocol::encode_random_draw_counts(counts, sink);
        FAIL("an unsafe random draw count reached the unsigned sink");
      } catch (const protocol::ProtocolEncodingError& error) {
        CHECK(error.error_code() == protocol::ProtocolEncodingErrorCode::kRandomDrawCountOutOfRange);
        CHECK(error.code() == "PROTOCOL.ENCODING.RANDOM_DRAW_COUNT_OUT_OF_RANGE");
        CHECK(error.context() == "snapshot_message.data.random_draw_counts." + std::string(stream.name));
        CHECK(error.detail() == "random draw count must be in the inclusive range 0 to 2^53-1");
      }
    }
  }
}
