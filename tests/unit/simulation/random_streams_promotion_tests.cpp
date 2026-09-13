#include "deterministic_random.hpp"
#include "fixtures/deterministic_random_frozen_reference.hpp"
#include "fixtures/random_stream_fixture.hpp"
#include "random_stream_registry.hpp"
#include "random_streams.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::random_stream_fixture;
namespace frozen = blob_royale::testing::deterministic_random_reference;

namespace {

struct ExtractedFinalizer final {
  [[nodiscard]] static std::uint64_t mix_bits(const std::uint64_t value) noexcept {
    return simulation::DeterministicRandom::mix_bits(value);
  }
};

using ExtractedGenerator = frozen::Random<ExtractedFinalizer>;
using Streams = simulation::RandomStreams;
using Kind = simulation::RandomStreamKind;

template <typename Value>
concept HasMutableTemporaryStream = requires(Value value) { std::move(value).get(Kind::kHazards); };

template <typename Value>
concept HasConstTemporaryStream =
    requires(const Value value) { std::move(value).get(Kind::kHazards); };

static_assert(!std::is_default_constructible_v<Streams>);
static_assert(std::is_copy_constructible_v<Streams>);
static_assert(std::is_copy_assignable_v<Streams>);
static_assert(std::is_nothrow_move_constructible_v<Streams>);
static_assert(std::is_nothrow_move_assignable_v<Streams>);
static_assert(std::is_nothrow_destructible_v<Streams>);
static_assert(noexcept(Streams::create(fixture::kOwnedSeed)));
static_assert(noexcept(std::declval<const Streams&>().draw_counts()));
static_assert(!HasMutableTemporaryStream<Streams>);
static_assert(!HasConstTemporaryStream<Streams>);
static_assert(std::same_as<decltype(std::declval<Streams&>().get(Kind::kHazards)),
                           simulation::DeterministicRandom&>);
static_assert(std::same_as<decltype(std::declval<const Streams&>().get(Kind::kHazards)),
                           const simulation::DeterministicRandom&>);
static_assert(std::same_as<decltype(std::declval<const Streams&>().draw_counts()),
                           simulation::RandomDrawCounts>);
static_assert(std::same_as<simulation::RandomDrawCounts::value_type, std::uint64_t>);
static_assert(noexcept(simulation::DeterministicRandom::mix_bits(fixture::kOwnedSeed)));
static_assert(noexcept(std::declval<simulation::DeterministicRandom&>().next_bits()));
static_assert(noexcept(std::declval<simulation::DeterministicRandom&>().next_unit_interval()));
static_assert(static_cast<std::uint8_t>(Kind::kHazards) == 0);
static_assert(static_cast<std::uint8_t>(Kind::kHill) == 1);
static_assert(simulation::kRandomStreamCount == 2);
static_assert(simulation::kRandomStreamRegistry[0].kind == Kind::kHazards);
static_assert(simulation::kRandomStreamRegistry[0].name == "hazards");
static_assert(simulation::kRandomStreamRegistry[1].kind == Kind::kHill);
static_assert(simulation::kRandomStreamRegistry[1].name == "hill");

// Unit draws are compared as binary64 bits, never with approximate numeric equality. The same
// operation specimen drives the production RNG, independent frozen RNG, and the extracted
// finalizer through frozen distributions, so promotion cannot hide behind two live callers
// changing.
template <typename Generator>
[[nodiscard]] std::uint64_t draw_result(Generator& generator, const fixture::Draw draw) {
  switch (draw.operation) {
  case fixture::DrawOperation::kBits:
    return generator.next_bits();
  case fixture::DrawOperation::kUnitInterval:
    return std::bit_cast<std::uint64_t>(generator.next_unit_interval());
  case fixture::DrawOperation::kBelow:
    return generator.next_below(draw.bound);
  }
  FAIL("unregistered operation in the named random-stream fixture");
  return 0;
}

template <typename First, typename Second, typename Third>
void compare_mixed_draws(First& first, Second& second, Third& third) {
  for (std::size_t cycle = 0; cycle < fixture::kMixedOperationCycles; ++cycle) {
    INFO("cycle " << cycle);
    for (const fixture::Draw draw : fixture::kDraws) {
      INFO("operation " << static_cast<int>(draw.operation) << " bound " << draw.bound);
      const std::uint64_t expected = draw_result(second, draw);
      CHECK(draw_result(first, draw) == expected);
      CHECK(draw_result(third, draw) == expected);
      CHECK(first.seed() == second.seed());
      CHECK(third.seed() == second.seed());
      CHECK(first.draw_count() == second.draw_count());
      CHECK(third.draw_count() == second.draw_count());
    }
  }
}

template <typename Invoke>
void check_invalid_kind(const fixture::InvalidKind& invalid, const Invoke& invoke) {
  try {
    invoke();
    FAIL("unregistered stream kind was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kRandomStreamKindInvalid);
    CHECK(error.code() == "SIMULATION.RANDOM_STREAM_KIND_INVALID");
    CHECK(error.context() == "random_stream.kind");
    CHECK(error.detail() == invalid.detail);
    CHECK(std::string_view(error.what()) == invalid.message);
  }
}

template <typename Generator> void check_rejection_specimen(Generator& generator) {
  CHECK(generator.next_below(fixture::kRejectionBound) == fixture::kRejectionResult);
  CHECK(generator.draw_count() == fixture::kRejectionDrawCount);
  const Generator before = generator;
  try {
    static_cast<void>(generator.next_below(0));
    FAIL("zero bound was accepted");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kDeterministicRandomBoundEmpty);
    CHECK(error.code() == "SIMULATION.DETERMINISTIC_RANDOM_BOUND_EMPTY");
    CHECK(error.context() == "deterministic_random.next_below.bound");
    CHECK(error.detail() == "a draw below zero names no value");
  }
  CHECK(generator == before);
}

} // namespace

TEST_CASE("random mixing matches literal unsigned finalizer goldens",
          "[unit][simulation][random_streams][promotion]") {
  for (const fixture::MixingGolden golden : fixture::kMixingGoldens) {
    INFO("input " << golden.input);
    CHECK(simulation::DeterministicRandom::mix_bits(golden.input) == golden.output);
    CHECK(frozen::FrozenFinalizer::mix_bits(golden.input) == golden.output);
  }
}

TEST_CASE("random mixing preserves the frozen legacy mixed distribution bits",
          "[unit][simulation][random_streams][promotion]") {
  for (const std::uint64_t seed : fixture::kMatchSeeds) {
    INFO("seed " << seed);
    simulation::DeterministicRandom production = simulation::DeterministicRandom::create(seed);
    frozen::DeterministicRandom reference = frozen::DeterministicRandom::create(seed);
    ExtractedGenerator extracted = ExtractedGenerator::create(seed);
    compare_mixed_draws(production, reference, extracted);
  }
}

TEST_CASE("random stream hazards preserve the legacy seed and every mixed draw",
          "[unit][simulation][random_streams][promotion]") {
  for (const std::uint64_t seed : fixture::kMatchSeeds) {
    INFO("seed " << seed);
    Streams streams = Streams::create(seed);
    simulation::DeterministicRandom production = simulation::DeterministicRandom::create(seed);
    frozen::DeterministicRandom reference = frozen::DeterministicRandom::create(seed);
    CHECK(streams.get(Kind::kHazards) == production);
    compare_mixed_draws(streams.get(Kind::kHazards), reference, production);
    CHECK(streams.get(Kind::kHazards) == production);
    CHECK(streams.get(Kind::kHill).draw_count() == 0);
  }
}

TEST_CASE("random stream promotion counts actual bounded rejection and preserves zero-bound errors",
          "[unit][simulation][random_streams][promotion]") {
  simulation::DeterministicRandom production =
      simulation::DeterministicRandom::create(fixture::kRejectionSeed);
  frozen::DeterministicRandom reference =
      frozen::DeterministicRandom::create(fixture::kRejectionSeed);
  ExtractedGenerator extracted = ExtractedGenerator::create(fixture::kRejectionSeed);
  Streams streams = Streams::create(fixture::kRejectionSeed);
  check_rejection_specimen(production);
  check_rejection_specimen(reference);
  check_rejection_specimen(extracted);
  check_rejection_specimen(streams.get(Kind::kHazards));
}

TEST_CASE("random stream initialization draws nothing and pins hill seed and sequence goldens",
          "[unit][simulation][random_streams][promotion]") {
  for (const fixture::HillGolden& golden : fixture::kHillGoldens) {
    INFO("match seed " << golden.match_seed);
    Streams streams = Streams::create(golden.match_seed);
    CHECK(streams.draw_counts() == simulation::RandomDrawCounts{});
    CHECK(streams.get(Kind::kHazards) ==
          simulation::DeterministicRandom::create(golden.match_seed));
    CHECK(streams.get(Kind::kHill).seed() == golden.hill_seed);
    frozen::DeterministicRandom hill_reference =
        frozen::DeterministicRandom::create(golden.hill_seed);
    for (const std::uint64_t expected : golden.draws) {
      CHECK(streams.get(Kind::kHill).next_bits() == expected);
      CHECK(hill_reference.next_bits() == expected);
    }
    CHECK(streams.get(Kind::kHill).draw_count() == golden.draws.size());
    CHECK(streams.get(Kind::kHazards) ==
          simulation::DeterministicRandom::create(golden.match_seed));
  }
}

TEST_CASE("random streams remain independent when either stream draws extra values",
          "[unit][simulation][random_streams][promotion]") {
  for (const simulation::RandomStreamDefinition extra : simulation::kRandomStreamRegistry) {
    INFO(extra.name);
    const Kind observed = extra.kind == Kind::kHazards ? Kind::kHill : Kind::kHazards;
    Streams streams = Streams::create(fixture::kOwnedSeed);
    Streams control = streams;
    const simulation::DeterministicRandom untouched = streams.get(observed);
    for (std::size_t draw = 0; draw < fixture::kIndependentDrawCount; ++draw) {
      static_cast<void>(streams.get(extra.kind).next_bits());
    }
    CHECK(streams.get(observed) == untouched);
    CHECK(streams.get(extra.kind).draw_count() == fixture::kIndependentDrawCount);
    for (std::size_t draw = 0; draw < fixture::kIndependentDrawCount; ++draw) {
      CHECK(streams.get(observed).next_bits() == control.get(observed).next_bits());
      static_cast<void>(streams.get(extra.kind).next_unit_interval());
    }
    CHECK(streams.get(observed) == control.get(observed));
  }
}

TEST_CASE("random streams own copied moved and assigned generator values",
          "[unit][simulation][random_streams][promotion]") {
  const Streams owned = [] {
    Streams source = Streams::create(fixture::kOwnedSeed);
    static_cast<void>(source.get(Kind::kHazards).next_bits());
    static_cast<void>(source.get(Kind::kHill).next_unit_interval());
    return source;
  }();
  Streams copied(owned);
  CHECK(copied == owned);
  CHECK(&copied.get(Kind::kHazards) != &owned.get(Kind::kHazards));
  CHECK(&copied.get(Kind::kHill) != &owned.get(Kind::kHill));

  const Streams moved(std::move(copied));
  CHECK(moved == owned);
  CHECK(copied == owned);
  CHECK(&moved.get(Kind::kHill) != &copied.get(Kind::kHill));

  Streams assigned = Streams::create(fixture::kDifferentSeed);
  assigned = owned;
  CHECK(assigned == owned);
  Streams move_assigned = Streams::create(fixture::kDifferentSeed);
  move_assigned = std::move(assigned);
  CHECK(move_assigned == owned);
  CHECK(assigned == owned);
  static_cast<void>(move_assigned.get(Kind::kHill).next_bits());
  CHECK_FALSE(move_assigned == owned);
  CHECK(assigned == owned);
}

TEST_CASE("random stream count values retain registry order without borrowing generator state",
          "[unit][simulation][random_streams][promotion]") {
  Streams streams = Streams::create(fixture::kOwnedSeed);
  static_cast<void>(streams.get(Kind::kHazards).next_bits());
  static_cast<void>(streams.get(Kind::kHill).next_bits());
  static_cast<void>(streams.get(Kind::kHill).next_bits());
  const simulation::RandomDrawCounts retained = streams.draw_counts();
  CHECK(retained[simulation::random_stream_index(Kind::kHazards)] == 1);
  CHECK(retained[simulation::random_stream_index(Kind::kHill)] == 2);
  static_cast<void>(streams.get(Kind::kHazards).next_bits());
  CHECK(retained[simulation::random_stream_index(Kind::kHazards)] == 1);
  CHECK(streams.draw_counts()[simulation::random_stream_index(Kind::kHazards)] == 2);

  simulation::RandomDrawCounts full_width{};
  full_width[simulation::random_stream_index(Kind::kHill)] =
      std::numeric_limits<std::uint64_t>::max();
  CHECK(full_width[simulation::random_stream_index(Kind::kHill)] ==
        std::numeric_limits<std::uint64_t>::max());
}

TEST_CASE("random stream runtime lookup rejects unknown kinds without mutating any generator",
          "[unit][simulation][random_streams][promotion]") {
  Streams streams = Streams::create(fixture::kOwnedSeed);
  const Streams before = streams;
  for (const fixture::InvalidKind& invalid : fixture::kInvalidKinds) {
    INFO("ordinal " << static_cast<unsigned>(invalid.ordinal));
    const Kind kind = static_cast<Kind>(invalid.ordinal);
    check_invalid_kind(invalid,
                       [kind] { static_cast<void>(simulation::random_stream_index(kind)); });
    check_invalid_kind(invalid,
                       [kind] { static_cast<void>(simulation::random_stream_name(kind)); });
    check_invalid_kind(invalid, [&streams, kind] { static_cast<void>(streams.get(kind)); });
    check_invalid_kind(invalid, [&before, kind] { static_cast<void>(before.get(kind)); });
    CHECK(streams == before);
  }
  for (const simulation::RandomStreamDefinition stream : simulation::kRandomStreamRegistry) {
    CHECK(simulation::random_stream_name(stream.kind) == stream.name);
    CHECK(simulation::kRandomStreamRegistry[simulation::random_stream_index(stream.kind)].kind ==
          stream.kind);
  }
}
