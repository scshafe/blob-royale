#include "application_config.hpp"
#include "application_config_loader.hpp"
#include "candidate_pair.hpp"
#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "commands/join_command.hpp"
#include "commands/spawn_command.hpp"
#include "commands/start_match_command.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_reservation.hpp"
#include "fixed_delta.hpp"
#include "game_mode.hpp"
#include "game_mode_configuration.hpp"
#include "game_mode_registry.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "map_loader.hpp"
#include "match_configuration.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics_body.hpp"
#include "player_snapshot.hpp"
#include "protocol_json_encoding.hpp"
#include "request_id.hpp"
#include "royale/royale_configuration.hpp"
#include "seat_roster.hpp"
#include "shared/hazard_archetype.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "snapshot_delivery_state.hpp"
#include "spatial_grid.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json.hpp>
#include <boost/version.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif

namespace blob_royale::benchmarks {
namespace {

namespace json = boost::json;
using Clock = std::chrono::steady_clock;
using simulation::CandidatePair;
using simulation::EntityId;
using simulation::FixedDelta;
using simulation::GameSimulation;
using simulation::GameWorld;
using simulation::InputBatch;
using simulation::PhysicsBody;
using EntitySeed = simulation::GameWorld::EntitySeed;
using simulation::SimulationConfig;
using simulation::SpatialGrid;
using simulation::Vector2;
using simulation::WorldSnapshot;

constexpr std::size_t kTimedSampleCount = 9;
constexpr std::size_t kWarmupRunCount = 1;
constexpr std::size_t kBackpressurePresentationSlotCount = 4'096;
constexpr std::size_t kBackpressureWriteCompletionInterval = 16;
constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr std::string_view kBenchmarkSchema = "blob-royale-benchmark-v1";
constexpr std::string_view kProtocolTimestamp = "2026-08-05T00:00:00.000Z";
constexpr std::string_view kExpectedToolchainId = "ubuntu-24.04-amd64-20260804";
constexpr std::string_view kExpectedBaseImageDigest =
    "sha256:019e8eb29a85e74d64925745884f2ec79aa27e3feab36353d24656f4d6b89467";

// The royale case: the deployed match, at the roster ADR 0006 budgets a room for.
//
// `docs/architecture/0006-lobbies-as-rooms.md` § "The tick-loop decision" sets the acceptance
// number for one room at **a mean step of at most 250 µs and a p99 of at most 1 ms** at the
// deployed roster -- eight seats, the deployed hazard table, the 32-marker map -- measured on the
// native runner. The kernel cases above are the engine's floor and say nothing about a live match
// (`benchmarks/README.md`); this case is the live match. Every number but the seat count is read
// from `deploy/ubuntu-pc/blob-royale.cfg` and the map it names, so the case cannot drift from the
// deployment; the seat count is widened from the deployed four to the eight the budget is stated
// for, and both values are in the output.
constexpr std::string_view kRoyaleCaseName = "royale_deployed_roster";
constexpr std::uint64_t kRoyaleSeatCount = 8;
// Sixteen seconds of `running`. The countdown holds the match for the deployed five seconds, so
// the timed ticks run from about tick 2,001 to about tick 8,400 and cross every spawn tick of both
// deployed hazard kinds -- the comet every 2,400 ticks and the boulder at tick 8,000 -- which a
// ten-second window would not. Nobody thrusts, and the 32 markers all sit within 233 wu of the
// centre while the zone is still above 480 wu at the end, so the zone eliminates nobody; the
// deployed comet is lethal, though, and the field thins as comets cross it, which is what a
// deployed match does. The output carries the player count at both ends of the window.
constexpr std::size_t kRoyaleRunningTicksPerSample = 6'400;
constexpr std::size_t kRoyaleSnapshotsPerSample = 200;
constexpr double kRoyaleStepMeanBudgetNanoseconds = 250'000.0;
constexpr double kRoyaleStepPercentile99BudgetNanoseconds = 1'000'000.0;

class BenchmarkError final : public std::runtime_error {
public:
  BenchmarkError(std::string code, std::string message)
      : std::runtime_error(message), code_(std::move(code)) {}

  [[nodiscard]] const std::string& code() const& noexcept { return code_; }
  [[nodiscard]] const std::string& code() const&& = delete;

private:
  std::string code_;
};

enum class Layout { kSparse, kClustered };

struct Scenario final {
  std::string_view name;
  std::size_t player_count;
  Layout layout;
  double world_width;
  double world_height;
  double player_radius;
  std::uint64_t grid_columns;
  std::uint64_t grid_rows;
  std::size_t ticks_per_sample;
  std::size_t grid_rebuilds_per_sample;
  std::size_t snapshots_per_sample;
  std::size_t encodings_per_sample;
};

struct RobustSummary final {
  double median;
  double percentile_25;
  double percentile_75;
  double median_absolute_deviation;
  double minimum;
  double maximum;
};

struct Measurement final {
  std::size_t operations_per_sample;
  std::vector<double> nanoseconds_per_operation;
};

struct BackpressureOutcome final {
  Measurement presentation_slots;
  std::size_t published_snapshot_count;
  std::size_t delivered_snapshot_count;
  std::size_t coalesced_snapshot_count;
  std::size_t maximum_retained_snapshot_count;
  std::uint64_t maximum_delivered_tick_lag;
  std::uint64_t final_delivered_tick_lag;
  std::string delivered_tick_trace_hash;
};

struct BackpressureSample final {
  std::size_t published_snapshot_count;
  std::size_t delivered_snapshot_count;
  std::size_t coalesced_snapshot_count;
  std::size_t maximum_retained_snapshot_count;
  std::uint64_t maximum_delivered_tick_lag;
  std::uint64_t final_delivered_tick_lag;
  std::string delivered_tick_trace_hash;
  std::optional<double> nanoseconds_per_presentation_slot;
};

// Counts distinct snapshot objects, not shared_ptr owners. Input references are moved into the
// production delivery state; subtracting the not-yet-presented inputs therefore exposes the exact
// number of snapshot objects whose lifetime is retained by delivery processing.
class SnapshotLifetimeTracker final {
public:
  [[nodiscard]] std::shared_ptr<const WorldSnapshot> track(WorldSnapshot snapshot) {
    auto owner = std::make_shared<TrackedSnapshot>(*this, std::move(snapshot));
    const WorldSnapshot* const snapshot_pointer = &owner->snapshot;
    return std::shared_ptr<const WorldSnapshot>{std::move(owner), snapshot_pointer};
  }

  [[nodiscard]] std::size_t live_snapshot_count() const noexcept { return live_snapshot_count_; }

private:
  struct TrackedSnapshot final {
    TrackedSnapshot(SnapshotLifetimeTracker& tracker, WorldSnapshot value)
        : tracker(tracker), snapshot(std::move(value)) {
      ++tracker.live_snapshot_count_;
    }

    ~TrackedSnapshot() { --tracker.live_snapshot_count_; }

    TrackedSnapshot(const TrackedSnapshot&) = delete;
    TrackedSnapshot& operator=(const TrackedSnapshot&) = delete;
    TrackedSnapshot(TrackedSnapshot&&) = delete;
    TrackedSnapshot& operator=(TrackedSnapshot&&) = delete;

    SnapshotLifetimeTracker& tracker;
    WorldSnapshot snapshot;
  };

  std::size_t live_snapshot_count_{0};
};

[[noreturn]] void fail(std::string code, std::string message) {
  throw BenchmarkError(std::move(code), std::move(message));
}

void require(const bool condition, const std::string_view code, const std::string_view message) {
  if (!condition) {
    fail(std::string(code), std::string(message));
  }
}

[[nodiscard]] std::string environment_value(const char* const name) {
  const char* const value = std::getenv(name);
  return value == nullptr ? "unavailable" : std::string(value);
}

[[nodiscard]] std::optional<std::string> read_first_line(const std::string& path) {
  std::ifstream input(path);
  std::string line;
  if (!input.is_open() || !std::getline(input, line)) {
    return std::nullopt;
  }
  return line;
}

[[nodiscard]] std::optional<std::string> read_prefixed_line(const std::string& path,
                                                            const std::string_view prefix) {
  std::ifstream input(path);
  std::string line;
  while (std::getline(input, line)) {
    if (line.starts_with(prefix)) {
      std::string value = line.substr(prefix.size());
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                                value.front() == ':' || value.front() == '=')) {
        value.erase(value.begin());
      }
      if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
      }
      return value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::uint64_t parse_memory_kib(const std::string& value) {
  std::istringstream parser(value);
  std::uint64_t kibibytes = 0;
  std::string unit;
  parser >> kibibytes >> unit;
  if (!parser || unit != "kB") {
    fail("BENCHMARK.MEMORY_METADATA_INVALID", "failed to parse /proc/meminfo MemTotal");
  }
  if (kibibytes > std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
    fail("BENCHMARK.MEMORY_METADATA_OVERFLOW", "physical-memory byte count overflowed");
  }
  return kibibytes * 1024ULL;
}

[[nodiscard]] std::string operating_system_name() {
  return read_prefixed_line("/etc/os-release", "PRETTY_NAME").value_or("unavailable");
}

[[nodiscard]] std::string cpu_model_name() {
  return read_prefixed_line("/proc/cpuinfo", "model name").value_or("unavailable");
}

[[nodiscard]] std::uint64_t physical_memory_bytes() {
  const auto memory = read_prefixed_line("/proc/meminfo", "MemTotal");
  if (!memory.has_value()) {
    fail("BENCHMARK.MEMORY_METADATA_MISSING", "failed to read /proc/meminfo MemTotal");
  }
  return parse_memory_kib(*memory);
}

[[nodiscard]] std::string cpu_governor() {
  return read_first_line("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
      .value_or("unavailable");
}

[[nodiscard]] std::uint64_t peak_resident_set_size_bytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    fail("BENCHMARK.GETRUSAGE_FAILED", "getrusage(RUSAGE_SELF) failed");
  }
  require(usage.ru_maxrss >= 0, "BENCHMARK.PEAK_RSS_INVALID",
          "getrusage returned a negative peak resident-set size");
  const auto maximum_resident_set_size_kib = static_cast<std::uint64_t>(usage.ru_maxrss);
  if (maximum_resident_set_size_kib > std::numeric_limits<std::uint64_t>::max() / 1024ULL) {
    fail("BENCHMARK.PEAK_RSS_OVERFLOW", "peak resident-set byte count overflowed");
  }
  return maximum_resident_set_size_kib * 1024ULL;
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime;
}

void hash_uint64(std::uint64_t& hash, const std::uint64_t value) noexcept {
  for (unsigned int shift = 0; shift < 64U; shift += 8U) {
    hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xffU));
  }
}

void hash_double(std::uint64_t& hash, const double value) noexcept {
  hash_uint64(hash, std::bit_cast<std::uint64_t>(value));
}

[[nodiscard]] std::string hash_label(const std::uint64_t hash) {
  std::ostringstream output;
  output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

[[nodiscard]] std::string snapshot_hash(const WorldSnapshot& snapshot) {
  std::uint64_t hash = kFnvOffsetBasis;
  hash_uint64(hash, snapshot.tick_sequence().value());
  hash_uint64(hash, static_cast<std::uint64_t>(snapshot.players().size()));
  for (const simulation::PlayerSnapshot& player : snapshot.players()) {
    hash_uint64(hash, player.entity_id().value());
    hash_double(hash, player.position().x());
    hash_double(hash, player.position().y());
    hash_double(hash, player.velocity().x());
    hash_double(hash, player.velocity().y());
    hash_double(hash, player.acceleration().x());
    hash_double(hash, player.acceleration().y());
  }
  return hash_label(hash);
}

[[nodiscard]] std::string candidate_pair_hash(const SpatialGrid& grid) {
  std::uint64_t hash = kFnvOffsetBasis;
  hash_uint64(hash, static_cast<std::uint64_t>(grid.candidate_pairs().size()));
  for (const CandidatePair& pair : grid.candidate_pairs()) {
    hash_uint64(hash, pair.lower_id().value());
    hash_uint64(hash, pair.higher_id().value());
  }
  return hash_label(hash);
}

[[nodiscard]] std::string byte_string_hash(const std::string_view bytes) {
  std::uint64_t hash = kFnvOffsetBasis;
  for (const unsigned char byte : bytes) {
    hash_byte(hash, byte);
  }
  return hash_label(hash);
}

[[nodiscard]] std::vector<std::shared_ptr<const WorldSnapshot>>
make_backpressure_snapshots(SnapshotLifetimeTracker& lifetime_tracker) {
  const SimulationConfig configuration =
      SimulationConfig::create(64.0, 64.0, 1.0, SimulationConfig::kRequiredTicksPerSecond, 1, 1);
  GameSimulation simulation =
      GameSimulation::create(configuration, GameWorld::create(std::vector<EntitySeed>{}));

  std::vector<std::shared_ptr<const WorldSnapshot>> snapshots;
  snapshots.reserve(kBackpressurePresentationSlotCount);
  for (std::size_t slot = 0; slot < kBackpressurePresentationSlotCount; ++slot) {
    simulation.step(FixedDelta::canonical(), InputBatch::empty());
    snapshots.push_back(lifetime_tracker.track(simulation.snapshot()));
  }
  return snapshots;
}

[[nodiscard]] std::string tick_trace_hash(const std::vector<std::uint64_t>& delivered_ticks) {
  std::uint64_t hash = kFnvOffsetBasis;
  hash_uint64(hash, static_cast<std::uint64_t>(delivered_ticks.size()));
  for (const std::uint64_t tick : delivered_ticks) {
    hash_uint64(hash, tick);
  }
  return hash_label(hash);
}

[[nodiscard]] std::vector<std::uint64_t> expected_backpressure_delivered_ticks() {
  std::vector<std::uint64_t> expected_ticks;
  expected_ticks.reserve(kBackpressurePresentationSlotCount / kBackpressureWriteCompletionInterval +
                         2U);
  for (std::uint64_t tick = 1U; tick <= kBackpressurePresentationSlotCount;
       tick += kBackpressureWriteCompletionInterval) {
    expected_ticks.push_back(tick);
  }
  if (expected_ticks.back() != kBackpressurePresentationSlotCount) {
    expected_ticks.push_back(kBackpressurePresentationSlotCount);
  }
  return expected_ticks;
}

[[nodiscard]] SimulationConfig make_configuration(const Scenario& scenario) {
  return SimulationConfig::create(scenario.world_width, scenario.world_height,
                                  scenario.player_radius, SimulationConfig::kRequiredTicksPerSecond,
                                  scenario.grid_columns, scenario.grid_rows);
}

[[nodiscard]] Vector2 player_position(const Scenario& scenario, const std::size_t index) {
  if (scenario.layout == Layout::kSparse) {
    const std::size_t column_count = static_cast<std::size_t>(scenario.grid_columns);
    const std::size_t column = index % column_count;
    const std::size_t row = index / column_count;
    const double cell_width = scenario.world_width / static_cast<double>(scenario.grid_columns);
    const double cell_height = scenario.world_height / static_cast<double>(scenario.grid_rows);
    return Vector2::create((static_cast<double>(column) + 0.5) * cell_width,
                           (static_cast<double>(row) + 0.5) * cell_height);
  }

  const auto cluster_columns =
      static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(scenario.player_count))));
  const std::size_t column = index % cluster_columns;
  const std::size_t row = index / cluster_columns;
  const double spacing = scenario.player_radius * 2.125;
  const double origin = scenario.player_radius + 1.0;
  return Vector2::create(origin + static_cast<double>(column) * spacing,
                         origin + static_cast<double>(row) * spacing);
}

[[nodiscard]] Vector2 player_velocity(const std::size_t index) {
  const auto x_bucket = static_cast<std::int64_t>(index % 7U) - 3;
  const auto y_bucket = static_cast<std::int64_t>(index % 5U) - 2;
  return Vector2::create(static_cast<double>(x_bucket) * 0.125,
                         static_cast<double>(y_bucket) * 0.1);
}

[[nodiscard]] Vector2 player_acceleration(const std::size_t index) {
  const auto x_sign = (index % 2U) == 0U ? 1.0 : -1.0;
  const auto y_sign = (index % 3U) == 0U ? -1.0 : 1.0;
  return Vector2::create(x_sign * 0.001, y_sign * 0.0005);
}

[[nodiscard]] GameWorld make_world(const Scenario& scenario) {
  std::vector<EntitySeed> players;
  players.reserve(scenario.player_count);
  for (std::size_t index = 0; index < scenario.player_count; ++index) {
    const PhysicsBody body = PhysicsBody::create(
        player_position(scenario, index), player_velocity(index), player_acceleration(index));
    players.push_back(
        EntitySeed::create(EntityId::create(static_cast<std::uint64_t>(index + 1U)), body));
  }
  return GameWorld::create(std::move(players));
}

[[nodiscard]] WorldSnapshot run_to_final_snapshot(const Scenario& scenario) {
  GameSimulation simulation =
      GameSimulation::create(make_configuration(scenario), make_world(scenario));
  for (std::size_t tick = 0; tick < scenario.ticks_per_sample; ++tick) {
    simulation.step(FixedDelta::canonical(), InputBatch::empty());
  }
  return simulation.snapshot();
}

[[nodiscard]] double duration_nanoseconds_per_operation(const Clock::time_point start,
                                                        const Clock::time_point end,
                                                        const std::size_t operation_count) {
  require(operation_count > 0U, "BENCHMARK.OPERATION_COUNT_ZERO",
          "timed operation count must be greater than zero");
  const auto elapsed_nanoseconds =
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  require(elapsed_nanoseconds > 0, "BENCHMARK.NON_POSITIVE_DURATION",
          "steady-clock sample duration must be positive");
  return static_cast<double>(elapsed_nanoseconds) / static_cast<double>(operation_count);
}

[[nodiscard]] double linear_quantile(const std::vector<double>& sorted_values,
                                     const double probability) {
  require(!sorted_values.empty(), "BENCHMARK.EMPTY_SUMMARY", "cannot summarize zero samples");
  const double position = probability * static_cast<double>(sorted_values.size() - 1U);
  const auto lower_index = static_cast<std::size_t>(std::floor(position));
  const auto upper_index = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower_index);
  return sorted_values[lower_index] +
         fraction * (sorted_values[upper_index] - sorted_values[lower_index]);
}

[[nodiscard]] RobustSummary summarize(const std::vector<double>& values) {
  require(!values.empty(), "BENCHMARK.EMPTY_SUMMARY", "cannot summarize zero samples");
  std::vector<double> sorted_values = values;
  std::ranges::sort(sorted_values);
  const double median = linear_quantile(sorted_values, 0.5);

  std::vector<double> absolute_deviations;
  absolute_deviations.reserve(values.size());
  for (const double value : values) {
    absolute_deviations.push_back(std::abs(value - median));
  }
  std::ranges::sort(absolute_deviations);

  return RobustSummary{.median = median,
                       .percentile_25 = linear_quantile(sorted_values, 0.25),
                       .percentile_75 = linear_quantile(sorted_values, 0.75),
                       .median_absolute_deviation = linear_quantile(absolute_deviations, 0.5),
                       .minimum = sorted_values.front(),
                       .maximum = sorted_values.back()};
}

[[nodiscard]] json::array encode_samples(const std::vector<double>& values) {
  json::array encoded;
  encoded.reserve(values.size());
  for (const double value : values) {
    encoded.emplace_back(value);
  }
  return encoded;
}

[[nodiscard]] json::object encode_summary(const RobustSummary& summary) {
  json::object encoded;
  encoded.emplace("median", summary.median);
  encoded.emplace("percentile_25", summary.percentile_25);
  encoded.emplace("percentile_75", summary.percentile_75);
  encoded.emplace("median_absolute_deviation", summary.median_absolute_deviation);
  encoded.emplace("minimum", summary.minimum);
  encoded.emplace("maximum", summary.maximum);
  return encoded;
}

[[nodiscard]] json::object encode_measurement(const Measurement& measurement,
                                              const std::string_view singular_unit,
                                              const std::string_view plural_unit) {
  std::vector<double> operations_per_second;
  operations_per_second.reserve(measurement.nanoseconds_per_operation.size());
  for (const double nanoseconds : measurement.nanoseconds_per_operation) {
    operations_per_second.push_back(1'000'000'000.0 / nanoseconds);
  }

  json::object nanoseconds;
  nanoseconds.emplace("unit", "nanoseconds_per_" + std::string(singular_unit));
  nanoseconds.emplace("samples", encode_samples(measurement.nanoseconds_per_operation));
  nanoseconds.emplace("summary", encode_summary(summarize(measurement.nanoseconds_per_operation)));

  json::object throughput;
  throughput.emplace("unit", std::string(plural_unit) + "_per_second");
  throughput.emplace("samples", encode_samples(operations_per_second));
  throughput.emplace("summary", encode_summary(summarize(operations_per_second)));

  json::object encoded;
  encoded.emplace("sample_count", measurement.nanoseconds_per_operation.size());
  encoded.emplace(std::string(plural_unit) + "_per_sample", measurement.operations_per_sample);
  encoded.emplace("latency", std::move(nanoseconds));
  encoded.emplace("throughput", std::move(throughput));
  return encoded;
}

[[nodiscard]] json::object encode_measurement(const Measurement& measurement) {
  return encode_measurement(measurement, "operation", "operations");
}

[[nodiscard]] Measurement measure_simulation_steps(const Scenario& scenario,
                                                   const std::string& expected_snapshot_hash) {
  Measurement measurement{.operations_per_sample = scenario.ticks_per_sample,
                          .nanoseconds_per_operation = {}};
  measurement.nanoseconds_per_operation.reserve(kTimedSampleCount);

  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    GameSimulation simulation =
        GameSimulation::create(make_configuration(scenario), make_world(scenario));
    const auto start = Clock::now();
    for (std::size_t tick = 0; tick < scenario.ticks_per_sample; ++tick) {
      simulation.step(FixedDelta::canonical(), InputBatch::empty());
    }
    const auto end = Clock::now();

    const WorldSnapshot final_snapshot = simulation.snapshot();
    require(final_snapshot.tick_sequence().value() == scenario.ticks_per_sample,
            "BENCHMARK.FINAL_TICK_MISMATCH", "timed simulation ended at the wrong tick");
    require(final_snapshot.players().size() == scenario.player_count,
            "BENCHMARK.FINAL_PLAYER_COUNT_MISMATCH",
            "timed simulation ended with the wrong player count");
    require(snapshot_hash(final_snapshot) == expected_snapshot_hash,
            "BENCHMARK.FINAL_SNAPSHOT_HASH_MISMATCH",
            "timed simulation final snapshot differs from the untimed reference");
    measurement.nanoseconds_per_operation.push_back(
        duration_nanoseconds_per_operation(start, end, scenario.ticks_per_sample));
  }
  return measurement;
}

[[nodiscard]] Measurement measure_grid_rebuilds(const Scenario& scenario,
                                                const std::size_t expected_candidate_pair_count,
                                                const std::string& expected_candidate_hash) {
  Measurement measurement{.operations_per_sample = scenario.grid_rebuilds_per_sample,
                          .nanoseconds_per_operation = {}};
  measurement.nanoseconds_per_operation.reserve(kTimedSampleCount);
  const SimulationConfig configuration = make_configuration(scenario);
  const GameWorld world = make_world(scenario);

  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    SpatialGrid grid = SpatialGrid::create(configuration, world);
    const auto start = Clock::now();
    for (std::size_t rebuild = 0; rebuild < scenario.grid_rebuilds_per_sample; ++rebuild) {
      grid = grid.rebuilt(world);
    }
    const auto end = Clock::now();

    require(grid.candidate_pairs().size() == expected_candidate_pair_count,
            "BENCHMARK.CANDIDATE_PAIR_COUNT_MISMATCH",
            "rebuilt grid produced a different candidate-pair count");
    require(candidate_pair_hash(grid) == expected_candidate_hash,
            "BENCHMARK.CANDIDATE_PAIR_HASH_MISMATCH",
            "rebuilt grid produced different canonical candidate pairs");
    measurement.nanoseconds_per_operation.push_back(
        duration_nanoseconds_per_operation(start, end, scenario.grid_rebuilds_per_sample));
  }
  return measurement;
}

[[nodiscard]] Measurement measure_snapshot_creation(const Scenario& scenario,
                                                    const std::string& expected_snapshot_hash) {
  GameSimulation simulation =
      GameSimulation::create(make_configuration(scenario), make_world(scenario));
  for (std::size_t tick = 0; tick < scenario.ticks_per_sample; ++tick) {
    simulation.step(FixedDelta::canonical(), InputBatch::empty());
  }

  Measurement measurement{.operations_per_sample = scenario.snapshots_per_sample,
                          .nanoseconds_per_operation = {}};
  measurement.nanoseconds_per_operation.reserve(kTimedSampleCount);
  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    std::optional<WorldSnapshot> final_snapshot;
    const auto start = Clock::now();
    for (std::size_t snapshot_index = 0; snapshot_index < scenario.snapshots_per_sample;
         ++snapshot_index) {
      final_snapshot = simulation.snapshot();
    }
    const auto end = Clock::now();
    require(final_snapshot.has_value(), "BENCHMARK.SNAPSHOT_RESULT_MISSING",
            "snapshot sample retained no result");
    require(snapshot_hash(*final_snapshot) == expected_snapshot_hash,
            "BENCHMARK.SNAPSHOT_HASH_MISMATCH",
            "snapshot creation returned a state different from the untimed reference");
    measurement.nanoseconds_per_operation.push_back(
        duration_nanoseconds_per_operation(start, end, scenario.snapshots_per_sample));
  }
  return measurement;
}

[[nodiscard]] Measurement measure_json_encoding(const Scenario& scenario,
                                                const WorldSnapshot& reference_snapshot,
                                                const std::string& expected_encoding_hash,
                                                const std::size_t expected_encoded_byte_count) {
  const protocol::RequestId request_id = protocol::RequestId::create("benchmark-request");
  Measurement measurement{.operations_per_sample = scenario.encodings_per_sample,
                          .nanoseconds_per_operation = {}};
  measurement.nanoseconds_per_operation.reserve(kTimedSampleCount);

  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    std::string encoded_snapshot;
    const auto start = Clock::now();
    for (std::size_t encoding = 0; encoding < scenario.encodings_per_sample; ++encoding) {
      encoded_snapshot =
          protocol::encode_snapshot_message(reference_snapshot, request_id, 1U, kProtocolTimestamp);
    }
    const auto end = Clock::now();

    require(encoded_snapshot.size() == expected_encoded_byte_count,
            "BENCHMARK.ENCODED_BYTE_COUNT_MISMATCH",
            "JSON encoding returned a different byte count");
    require(byte_string_hash(encoded_snapshot) == expected_encoding_hash,
            "BENCHMARK.ENCODING_HASH_MISMATCH", "JSON encoding differs from the untimed reference");
    measurement.nanoseconds_per_operation.push_back(
        duration_nanoseconds_per_operation(start, end, scenario.encodings_per_sample));
  }
  return measurement;
}

// ---------------------------------------------------------------------------------------------
// The royale case.

// Where the deployment's inputs are on this machine: the runner passes both paths.
struct RoyaleCaseInputs final {
  std::filesystem::path deployment_configuration;
  std::filesystem::path maps_directory;
};

// Everything one identical royale simulation is built from, resolved once from the deployment.
struct RoyaleMatch final {
  SimulationConfig configuration;
  simulation::MapDefinition map;
  gameplay::GameModeConfiguration mode_configuration;
  std::string mode_name;
  std::uint64_t seed;
  std::uint64_t deployed_lobby_seat_count;
  std::string configuration_path;
};

// Per-tick step statistics of one timed sample, in nanoseconds.
struct TickStatistics final {
  double mean;
  double median;
  double percentile_99;
  double maximum;
};

// Reads the deployed configuration through the production loader and the named map through the
// production map loader, then widens the lobby to the budgeted seat count and nothing else.
[[nodiscard]] RoyaleMatch load_royale_match(const RoyaleCaseInputs& inputs) {
  const std::string configuration_path = inputs.deployment_configuration.string();
  const char* const arguments[] = {"blob_simulation_benchmarks", "--config",
                                   configuration_path.c_str()};
  application::ApplicationConfigLoader::Result loaded =
      application::ApplicationConfigLoader::load(3, arguments);
  const auto* const run_request =
      std::get_if<application::ApplicationConfigLoader::RunRequest>(&loaded);
  require(run_request != nullptr, "BENCHMARK.DEPLOYMENT_CONFIGURATION_INVALID",
          "the deployment configuration did not load as a run request");
  const application::ApplicationConfig& configuration = run_request->application_config();
  const application::MatchConfiguration& match = configuration.match_configuration();
  require(match.mode_name() == "royale", "BENCHMARK.DEPLOYMENT_MODE_UNEXPECTED",
          "the royale case requires a deployment whose [match] mode is royale");

  // The map is read from the repository's `maps/`, not from the container path the deployed
  // configuration names, which exists only inside the deployed container.
  simulation::MapDefinition map =
      application::MapLoader::load(inputs.maps_directory / match.map_name());

  // The seat count is a `[match]` fact and the lobby is world state, so the widening to eight seats
  // is done on the initial world's roster in `RoyaleRun` and the mode configuration is the
  // deployment's own, untouched.
  gameplay::GameModeConfiguration mode_configuration = configuration.game_mode_configuration();
  require(!mode_configuration.hazards.empty(), "BENCHMARK.DEPLOYMENT_HAZARDS_MISSING",
          "the royale case expects the deployment to declare at least one hazard kind");
  require(map.spawn_points().size() >= kRoyaleSeatCount, "BENCHMARK.DEPLOYMENT_MAP_TOO_SMALL",
          "the royale case needs a spawn marker for each of its eight seats");

  return RoyaleMatch{.configuration = configuration.simulation_config(),
                     .map = std::move(map),
                     .mode_configuration = std::move(mode_configuration),
                     .mode_name = match.mode_name(),
                     .seed = match.seed(),
                     .deployed_lobby_seat_count = match.lobby_seat_count(),
                     .configuration_path = configuration_path};
}

// One royale simulation driven the way the runtime drives it: a reservation of
// `spawn_count + kSystemCreatedEntityHeadroom` ids on every tick, opening above the map's static
// bodies, exactly as `runtime::EntityIdAllocator` and the replay fixture size it.
class RoyaleRun final {
public:
  explicit RoyaleRun(const RoyaleMatch& match)
      : simulation_(create_simulation(match)),
        next_entity_id_(simulation::kMinimumEntityId +
                        static_cast<std::uint64_t>(match.map.static_bodies().size())),
        countdown_ticks_(match.mode_configuration.royale.countdown_ticks()) {}

  // Tick 1 is the whole lobby: eight spawns, eight joins naming no seat, and one Start. The joins
  // take seats 0 to 7 in controller order and the objective commits `countdown` at the end of the
  // tick; empty ticks then carry the match to `running`.
  void reach_running() {
    std::vector<simulation::Command> commands;
    commands.reserve(2U * kRoyaleSeatCount + 1U);
    for (std::uint64_t controller = 1; controller <= kRoyaleSeatCount; ++controller) {
      const simulation::ControllerId id = simulation::ControllerId::create(controller);
      commands.emplace_back(simulation::SpawnCommand{id});
      commands.emplace_back(simulation::JoinCommand{id, std::nullopt});
    }
    commands.emplace_back(simulation::StartMatchCommand{simulation::ControllerId::create(1)});
    simulation_.step(FixedDelta::canonical(), batch(std::move(commands), kRoyaleSeatCount));

    const std::uint64_t deadline = simulation_.tick_sequence().value() + countdown_ticks_ + 16U;
    while (committed_phase() != simulation::MatchPhase::kRunning) {
      require(simulation_.tick_sequence().value() < deadline, "BENCHMARK.ROYALE_NEVER_RAN",
              "the royale match did not reach running within its countdown");
      step_empty();
    }
  }

  void step_empty() { simulation_.step(FixedDelta::canonical(), batch({}, 0)); }

  [[nodiscard]] WorldSnapshot snapshot() const { return simulation_.snapshot(); }
  [[nodiscard]] std::uint64_t tick_sequence() const noexcept {
    return simulation_.tick_sequence().value();
  }

private:
  [[nodiscard]] static GameSimulation create_simulation(const RoyaleMatch& match) {
    GameWorld world = GameWorld::create(match.configuration, match.map, match.seed);
    world.mutable_match().seats =
        simulation::SeatRoster::of_size(static_cast<std::size_t>(kRoyaleSeatCount));
    std::unique_ptr<const simulation::GameMode> mode =
        gameplay::GameModeRegistry::create(match.mode_name, match.mode_configuration);
    return GameSimulation::create(
        match.configuration, std::move(world),
        simulation::GameSimulationSetup::of_mode(match.map, std::move(mode)));
  }

  [[nodiscard]] InputBatch batch(std::vector<simulation::Command> commands,
                                 const std::uint64_t spawn_count) {
    const std::uint64_t width = spawn_count + simulation::kSystemCreatedEntityHeadroom;
    const simulation::EntityIdReservation reservation =
        simulation::EntityIdReservation::create(EntityId::create(next_entity_id_), width);
    next_entity_id_ += width;
    return InputBatch::create(std::move(commands), simulation_.accepted_command_kinds(),
                              reservation);
  }

  [[nodiscard]] simulation::MatchPhase committed_phase() const {
    const WorldSnapshot current = simulation_.snapshot();
    return current.match().phase();
  }

  GameSimulation simulation_;
  std::uint64_t next_entity_id_;
  std::uint64_t countdown_ticks_;
};

[[nodiscard]] TickStatistics summarize_ticks(std::vector<double> tick_nanoseconds) {
  require(!tick_nanoseconds.empty(), "BENCHMARK.EMPTY_SUMMARY", "cannot summarize zero ticks");
  double total = 0.0;
  for (const double value : tick_nanoseconds) {
    total += value;
  }
  std::ranges::sort(tick_nanoseconds);
  return TickStatistics{.mean = total / static_cast<double>(tick_nanoseconds.size()),
                        .median = linear_quantile(tick_nanoseconds, 0.5),
                        .percentile_99 = linear_quantile(tick_nanoseconds, 0.99),
                        .maximum = tick_nanoseconds.back()};
}

// The untimed reference: the world the timed samples must reproduce bit for bit.
[[nodiscard]] WorldSnapshot run_royale_reference(const RoyaleMatch& match) {
  RoyaleRun run(match);
  run.reach_running();
  for (std::size_t tick = 0; tick < kRoyaleRunningTicksPerSample; ++tick) {
    run.step_empty();
  }
  return run.snapshot();
}

[[nodiscard]] json::object encode_tick_statistics(const TickStatistics& statistics) {
  json::object encoded;
  encoded.emplace("mean", statistics.mean);
  encoded.emplace("median", statistics.median);
  encoded.emplace("percentile_99", statistics.percentile_99);
  encoded.emplace("maximum", statistics.maximum);
  return encoded;
}

[[nodiscard]] json::object benchmark_royale_case(const RoyaleCaseInputs& inputs) {
  const RoyaleMatch match = load_royale_match(inputs);
  const WorldSnapshot reference = run_royale_reference(match);
  const std::string reference_hash = snapshot_hash(reference);
  require(snapshot_hash(run_royale_reference(match)) == reference_hash,
          "BENCHMARK.ROYALE_REFERENCE_HASH_MISMATCH",
          "independent untimed royale matches produced different final snapshots");
  const std::uint64_t expected_final_tick = reference.tick_sequence().value();
  const std::uint64_t first_timed_tick = expected_final_tick - kRoyaleRunningTicksPerSample + 1U;

  for (std::size_t warmup = 0; warmup < kWarmupRunCount; ++warmup) {
    static_cast<void>(run_royale_reference(match));
  }

  // Timed: every `step` of the running window individually, so the mean the budget is stated in
  // and the p99 it is stated in are both per tick. The region includes building the tick's empty
  // batch and reservation, which every production tick builds too.
  std::vector<TickStatistics> step_samples;
  step_samples.reserve(kTimedSampleCount);
  Measurement snapshots{.operations_per_sample = kRoyaleSnapshotsPerSample,
                        .nanoseconds_per_operation = {}};
  snapshots.nanoseconds_per_operation.reserve(kTimedSampleCount);
  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    RoyaleRun run(match);
    run.reach_running();
    require(run.tick_sequence() + 1U == first_timed_tick, "BENCHMARK.ROYALE_START_TICK_MISMATCH",
            "a timed royale match reached running on a different tick than the reference");
    std::vector<double> tick_nanoseconds;
    tick_nanoseconds.reserve(kRoyaleRunningTicksPerSample);
    for (std::size_t tick = 0; tick < kRoyaleRunningTicksPerSample; ++tick) {
      const auto start = Clock::now();
      run.step_empty();
      const auto end = Clock::now();
      tick_nanoseconds.push_back(duration_nanoseconds_per_operation(start, end, 1U));
    }
    require(run.tick_sequence() == expected_final_tick, "BENCHMARK.FINAL_TICK_MISMATCH",
            "timed royale match ended at the wrong tick");
    require(snapshot_hash(run.snapshot()) == reference_hash,
            "BENCHMARK.FINAL_SNAPSHOT_HASH_MISMATCH",
            "timed royale match final snapshot differs from the untimed reference");
    step_samples.push_back(summarize_ticks(std::move(tick_nanoseconds)));

    // The snapshot of a running royale world, timed on the same world the steps just produced.
    std::optional<WorldSnapshot> last_snapshot;
    const auto snapshots_start = Clock::now();
    for (std::size_t index = 0; index < kRoyaleSnapshotsPerSample; ++index) {
      last_snapshot = run.snapshot();
    }
    const auto snapshots_end = Clock::now();
    require(last_snapshot.has_value() && snapshot_hash(*last_snapshot) == reference_hash,
            "BENCHMARK.SNAPSHOT_HASH_MISMATCH",
            "royale snapshot creation returned a state different from the untimed reference");
    snapshots.nanoseconds_per_operation.push_back(duration_nanoseconds_per_operation(
        snapshots_start, snapshots_end, kRoyaleSnapshotsPerSample));
  }

  std::vector<double> means;
  std::vector<double> medians;
  std::vector<double> percentiles_99;
  std::vector<double> maxima;
  json::array step_sample_documents;
  for (const TickStatistics& statistics : step_samples) {
    means.push_back(statistics.mean);
    medians.push_back(statistics.median);
    percentiles_99.push_back(statistics.percentile_99);
    maxima.push_back(statistics.maximum);
    step_sample_documents.emplace_back(encode_tick_statistics(statistics));
  }
  const RobustSummary mean_summary = summarize(means);
  const RobustSummary percentile_99_summary = summarize(percentiles_99);

  json::object step_summary;
  step_summary.emplace("mean", encode_summary(mean_summary));
  step_summary.emplace("median", encode_summary(summarize(medians)));
  step_summary.emplace("percentile_99", encode_summary(percentile_99_summary));
  step_summary.emplace("maximum", encode_summary(summarize(maxima)));

  json::object step_measurement;
  step_measurement.emplace("unit", "nanoseconds_per_tick");
  step_measurement.emplace("sample_count", step_samples.size());
  step_measurement.emplace("ticks_per_sample", kRoyaleRunningTicksPerSample);
  step_measurement.emplace("samples", std::move(step_sample_documents));
  step_measurement.emplace("summary", std::move(step_summary));

  json::object measurements;
  measurements.emplace("simulation_step_per_tick", std::move(step_measurement));
  measurements.emplace("snapshot_creation", encode_measurement(snapshots, "snapshot", "snapshots"));

  // The ADR's budget, read against the median across samples of each per-sample statistic. It is
  // reported, never enforced: whether the host is the one the budget is stated for is a fact about
  // the run's `platform` block, which the README says to read first.
  json::object budget;
  budget.emplace("mean_nanoseconds_per_tick_at_most", kRoyaleStepMeanBudgetNanoseconds);
  budget.emplace("percentile_99_nanoseconds_per_tick_at_most",
                 kRoyaleStepPercentile99BudgetNanoseconds);
  budget.emplace("mean_within_budget", mean_summary.median <= kRoyaleStepMeanBudgetNanoseconds);
  budget.emplace("percentile_99_within_budget",
                 percentile_99_summary.median <= kRoyaleStepPercentile99BudgetNanoseconds);
  budget.emplace("stated_in", "docs/architecture/0006-lobbies-as-rooms.md");

  json::array hazards;
  for (const gameplay::HazardArchetype& archetype : match.mode_configuration.hazards) {
    json::object hazard;
    hazard.emplace("kind", archetype.kind_name());
    hazard.emplace("radius", archetype.radius());
    hazard.emplace("mass", archetype.mass());
    hazard.emplace("speed", archetype.speed());
    hazard.emplace("spawn_interval_ticks", archetype.spawn_interval_ticks());
    hazard.emplace("lethal_on_contact", archetype.lethal_on_contact());
    hazards.emplace_back(std::move(hazard));
  }

  json::object deployment;
  deployment.emplace("configuration", match.configuration_path);
  deployment.emplace("mode", match.mode_name);
  deployment.emplace("map", match.map.name());
  deployment.emplace("spawn_marker_count", match.map.spawn_points().size());
  deployment.emplace("seed", match.seed);
  deployment.emplace("drag_per_second", match.configuration.drag_per_second());
  deployment.emplace("deployed_lobby_seat_count", match.deployed_lobby_seat_count);
  deployment.emplace("benchmark_lobby_seat_count", kRoyaleSeatCount);
  deployment.emplace("countdown_ticks", match.mode_configuration.royale.countdown_ticks());
  deployment.emplace("hazards", std::move(hazards));

  json::object correctness;
  correctness.emplace("first_timed_tick_sequence", first_timed_tick);
  correctness.emplace("expected_final_tick_sequence", expected_final_tick);
  correctness.emplace("initial_player_count", kRoyaleSeatCount);
  correctness.emplace("final_player_count", reference.players().size());
  correctness.emplace("final_entity_count", reference.entities().size());
  correctness.emplace("final_snapshot_hash", reference_hash);

  json::object result;
  result.emplace("name", kRoyaleCaseName);
  result.emplace("player_count", kRoyaleSeatCount);
  result.emplace("deployment", std::move(deployment));
  result.emplace("correctness", std::move(correctness));
  result.emplace("measurements", std::move(measurements));
  result.emplace("budget", std::move(budget));
  return result;
}

void record_retained_snapshot_count(const SnapshotLifetimeTracker& lifetime_tracker,
                                    const std::size_t future_input_count,
                                    std::size_t& maximum_retained_snapshot_count) {
  require(lifetime_tracker.live_snapshot_count() >= future_input_count,
          "BENCHMARK.BACKPRESSURE_LIFETIME_COUNT_INVALID",
          "tracked snapshot lifetime count fell below the remaining input count");
  const std::size_t retained_snapshot_count =
      lifetime_tracker.live_snapshot_count() - future_input_count;
  maximum_retained_snapshot_count =
      std::max(maximum_retained_snapshot_count, retained_snapshot_count);
}

void require_same_backpressure_outcome(const BackpressureSample& actual,
                                       const BackpressureSample& expected) {
  require(actual.published_snapshot_count == expected.published_snapshot_count &&
              actual.delivered_snapshot_count == expected.delivered_snapshot_count &&
              actual.coalesced_snapshot_count == expected.coalesced_snapshot_count &&
              actual.maximum_retained_snapshot_count == expected.maximum_retained_snapshot_count &&
              actual.maximum_delivered_tick_lag == expected.maximum_delivered_tick_lag &&
              actual.final_delivered_tick_lag == expected.final_delivered_tick_lag &&
              actual.delivered_tick_trace_hash == expected.delivered_tick_trace_hash,
          "BENCHMARK.BACKPRESSURE_OUTCOME_DRIFT",
          "identical backpressure samples produced different logical results");
}

[[nodiscard]] std::uint64_t require_active_tick(const std::optional<std::uint64_t>& active_tick,
                                                const std::string_view error_code,
                                                const std::string_view error_message) {
  if (!active_tick.has_value()) {
    fail(std::string(error_code), std::string(error_message));
  }
  return *active_tick;
}

[[nodiscard]] BackpressureSample run_delivery_backpressure_sample(const bool measure_duration) {
  SnapshotLifetimeTracker lifetime_tracker;
  std::vector<std::shared_ptr<const WorldSnapshot>> snapshots =
      make_backpressure_snapshots(lifetime_tracker);
  require(snapshots.size() == kBackpressurePresentationSlotCount,
          "BENCHMARK.BACKPRESSURE_SNAPSHOT_COUNT_MISMATCH",
          "backpressure input did not contain the required presentation slots");
  require(lifetime_tracker.live_snapshot_count() == snapshots.size(),
          "BENCHMARK.BACKPRESSURE_INITIAL_LIFETIME_COUNT_MISMATCH",
          "backpressure input lifetime tracking did not observe every snapshot");

  const std::uint64_t final_published_tick = snapshots.back()->tick_sequence().value();
  server::SnapshotDeliveryState delivery_state;
  std::vector<std::uint64_t> delivered_ticks;
  delivered_ticks.reserve(
      kBackpressurePresentationSlotCount / kBackpressureWriteCompletionInterval + 2U);
  std::optional<std::uint64_t> active_tick;
  std::uint64_t maximum_delivered_tick_lag = 0;
  std::size_t maximum_retained_snapshot_count = 0;

  const std::optional<Clock::time_point> start =
      measure_duration ? std::optional<Clock::time_point>{Clock::now()} : std::nullopt;
  for (std::size_t slot = 0; slot < snapshots.size(); ++slot) {
    const std::uint64_t published_tick = snapshots[slot]->tick_sequence().value();
    delivery_state.observe(std::move(snapshots[slot]));
    if (delivery_state.ready_to_write()) {
      const std::optional<server::SnapshotDelivery> delivery = delivery_state.begin_active_write();
      require(delivery.has_value(), "BENCHMARK.BACKPRESSURE_DELIVERY_MISSING",
              "ready delivery state did not start an active write");
      active_tick = delivery->snapshot->tick_sequence().value();
      delivered_ticks.push_back(*active_tick);
    }

    const std::size_t future_input_count = snapshots.size() - slot - 1U;
    record_retained_snapshot_count(lifetime_tracker, future_input_count,
                                   maximum_retained_snapshot_count);

    if ((slot + 1U) % kBackpressureWriteCompletionInterval == 0U && delivery_state.write_active()) {
      const std::uint64_t completed_tick =
          require_active_tick(active_tick, "BENCHMARK.BACKPRESSURE_ACTIVE_TICK_MISSING",
                              "delivery state reported an active write without a delivered tick");
      require(completed_tick <= published_tick, "BENCHMARK.BACKPRESSURE_TICK_ORDER_INVALID",
              "delivery state started a snapshot newer than the latest publication");
      maximum_delivered_tick_lag =
          std::max(maximum_delivered_tick_lag, published_tick - completed_tick);
      delivery_state.complete_active_write();
      active_tick.reset();
      record_retained_snapshot_count(lifetime_tracker, future_input_count,
                                     maximum_retained_snapshot_count);
    }
  }
  const std::optional<Clock::time_point> end =
      measure_duration ? std::optional<Clock::time_point>{Clock::now()} : std::nullopt;

  if (delivery_state.write_active()) {
    const std::uint64_t completed_tick =
        require_active_tick(active_tick, "BENCHMARK.BACKPRESSURE_FINAL_ACTIVE_TICK_MISSING",
                            "delivery state retained an unidentified final active write");
    require(completed_tick <= final_published_tick,
            "BENCHMARK.BACKPRESSURE_FINAL_TICK_ORDER_INVALID",
            "delivery state retained a snapshot newer than the final publication");
    maximum_delivered_tick_lag =
        std::max(maximum_delivered_tick_lag, final_published_tick - completed_tick);
    delivery_state.complete_active_write();
    active_tick.reset();
  }

  // Completing a write never bursts the coalesced pending value. Drain it in a synthetic later
  // presentation slot after the measured region so the metric still represents exactly 4,096
  // production presentation slots.
  if (delivery_state.ready_to_write()) {
    {
      const std::optional<server::SnapshotDelivery> final_delivery =
          delivery_state.begin_active_write();
      require(final_delivery.has_value(), "BENCHMARK.BACKPRESSURE_FINAL_DELIVERY_MISSING",
              "draining delivery state did not start its pending write");
      delivered_ticks.push_back(final_delivery->snapshot->tick_sequence().value());
      record_retained_snapshot_count(lifetime_tracker, 0U, maximum_retained_snapshot_count);
      delivery_state.complete_active_write();
    }
  }

  require(!delivery_state.write_active() && !delivery_state.has_pending_snapshot(),
          "BENCHMARK.BACKPRESSURE_NOT_DRAINED",
          "backpressure state retained data after the final delivery completed");
  require(lifetime_tracker.live_snapshot_count() == 0U,
          "BENCHMARK.BACKPRESSURE_SNAPSHOT_LIFETIME_LEAKED",
          "snapshot objects remained alive after delivery state drained");
  require(delivery_state.last_delivered_tick().has_value(),
          "BENCHMARK.BACKPRESSURE_FINAL_TICK_MISSING",
          "backpressure state did not commit a delivered tick");
  const std::uint64_t final_delivered_tick = *delivery_state.last_delivered_tick();
  require(final_delivered_tick == final_published_tick,
          "BENCHMARK.BACKPRESSURE_LATEST_NOT_DELIVERED",
          "drained delivery state did not deliver the newest publication");
  require(maximum_retained_snapshot_count == 2U, "BENCHMARK.BACKPRESSURE_RETENTION_RESULT_MISMATCH",
          "delivery state did not retain exactly the expected active and pending snapshot bound");
  require(delivery_state.delivered_message_count() == delivered_ticks.size(),
          "BENCHMARK.BACKPRESSURE_MESSAGE_COUNT_MISMATCH",
          "delivery state message sequence differs from observed deliveries");
  require(delivered_ticks == expected_backpressure_delivered_ticks(),
          "BENCHMARK.BACKPRESSURE_DELIVERED_TICKS_MISMATCH",
          "delivery state produced an unexpected deterministic delivery trace");
  require(maximum_delivered_tick_lag == kBackpressureWriteCompletionInterval - 1U,
          "BENCHMARK.BACKPRESSURE_MAXIMUM_LAG_MISMATCH",
          "delivery state produced an unexpected maximum tick lag");

  const std::size_t delivered_snapshot_count = delivered_ticks.size();
  require(delivered_snapshot_count <= snapshots.size(),
          "BENCHMARK.BACKPRESSURE_DELIVERY_COUNT_INVALID",
          "delivery state delivered more snapshots than were published");
  std::optional<double> nanoseconds_per_presentation_slot;
  if (start.has_value() && end.has_value()) {
    nanoseconds_per_presentation_slot =
        duration_nanoseconds_per_operation(*start, *end, snapshots.size());
  }
  return BackpressureSample{.published_snapshot_count = snapshots.size(),
                            .delivered_snapshot_count = delivered_snapshot_count,
                            .coalesced_snapshot_count = snapshots.size() - delivered_snapshot_count,
                            .maximum_retained_snapshot_count = maximum_retained_snapshot_count,
                            .maximum_delivered_tick_lag = maximum_delivered_tick_lag,
                            .final_delivered_tick_lag = final_published_tick - final_delivered_tick,
                            .delivered_tick_trace_hash = tick_trace_hash(delivered_ticks),
                            .nanoseconds_per_presentation_slot = nanoseconds_per_presentation_slot};
}

[[nodiscard]] BackpressureOutcome measure_delivery_backpressure() {
  std::optional<BackpressureSample> reference_sample;
  for (std::size_t warmup = 0; warmup < kWarmupRunCount; ++warmup) {
    BackpressureSample warmup_sample = run_delivery_backpressure_sample(false);
    require(!warmup_sample.nanoseconds_per_presentation_slot.has_value(),
            "BENCHMARK.BACKPRESSURE_WARMUP_WAS_TIMED",
            "untimed backpressure warm-up unexpectedly produced a timing sample");
    if (reference_sample.has_value()) {
      require_same_backpressure_outcome(warmup_sample, *reference_sample);
    } else {
      reference_sample = std::move(warmup_sample);
    }
  }
  require(reference_sample.has_value(), "BENCHMARK.BACKPRESSURE_REFERENCE_MISSING",
          "no untimed backpressure warm-up result was produced");

  Measurement measurement{.operations_per_sample = kBackpressurePresentationSlotCount,
                          .nanoseconds_per_operation = {}};
  measurement.nanoseconds_per_operation.reserve(kTimedSampleCount);
  for (std::size_t sample = 0; sample < kTimedSampleCount; ++sample) {
    const BackpressureSample timed_sample = run_delivery_backpressure_sample(true);
    require(timed_sample.nanoseconds_per_presentation_slot.has_value(),
            "BENCHMARK.BACKPRESSURE_TIMING_MISSING",
            "timed backpressure sample did not produce a presentation-slot duration");
    require_same_backpressure_outcome(timed_sample, *reference_sample);
    measurement.nanoseconds_per_operation.push_back(
        *timed_sample.nanoseconds_per_presentation_slot);
  }

  return BackpressureOutcome{
      .presentation_slots = std::move(measurement),
      .published_snapshot_count = reference_sample->published_snapshot_count,
      .delivered_snapshot_count = reference_sample->delivered_snapshot_count,
      .coalesced_snapshot_count = reference_sample->coalesced_snapshot_count,
      .maximum_retained_snapshot_count = reference_sample->maximum_retained_snapshot_count,
      .maximum_delivered_tick_lag = reference_sample->maximum_delivered_tick_lag,
      .final_delivered_tick_lag = reference_sample->final_delivered_tick_lag,
      .delivered_tick_trace_hash = std::move(reference_sample->delivered_tick_trace_hash)};
}

[[nodiscard]] json::object encode_backpressure_outcome(const BackpressureOutcome& outcome) {
  json::object policy;
  policy.emplace("presentation_slot_count", kBackpressurePresentationSlotCount);
  policy.emplace("simulated_write_completion_interval_slots", kBackpressureWriteCompletionInterval);
  policy.emplace("active_write_maximum_count", 1);
  policy.emplace("pending_snapshot_maximum_count", 1);
  policy.emplace("retention_measurement", "tracked_snapshot_object_lifetime");

  json::object correctness;
  correctness.emplace("published_snapshot_count", outcome.published_snapshot_count);
  correctness.emplace("delivered_snapshot_count", outcome.delivered_snapshot_count);
  correctness.emplace("coalesced_snapshot_count", outcome.coalesced_snapshot_count);
  correctness.emplace("maximum_retained_snapshot_count", outcome.maximum_retained_snapshot_count);
  correctness.emplace("maximum_delivered_tick_lag", outcome.maximum_delivered_tick_lag);
  correctness.emplace("final_delivered_tick_lag", outcome.final_delivered_tick_lag);
  correctness.emplace("delivered_tick_trace_hash", outcome.delivered_tick_trace_hash);

  json::object encoded;
  encoded.emplace("production_component", "blob_royale::server::SnapshotDeliveryState");
  encoded.emplace("policy", std::move(policy));
  encoded.emplace("correctness", std::move(correctness));
  encoded.emplace(
      "presentation_slot",
      encode_measurement(outcome.presentation_slots, "presentation_slot", "presentation_slots"));
  return encoded;
}

void run_warmup(const Scenario& scenario, const WorldSnapshot& reference_snapshot) {
  for (std::size_t warmup = 0; warmup < kWarmupRunCount; ++warmup) {
    static_cast<void>(run_to_final_snapshot(scenario));
    const SimulationConfig configuration = make_configuration(scenario);
    const GameWorld world = make_world(scenario);
    const SpatialGrid grid = SpatialGrid::create(configuration, world);
    static_cast<void>(grid.rebuilt(world));
    static_cast<void>(reference_snapshot.players().size());
    static_cast<void>(protocol::encode_snapshot_message(
        reference_snapshot, protocol::RequestId::create("benchmark-warmup"), 1U,
        kProtocolTimestamp));
  }
}

[[nodiscard]] json::object benchmark_scenario(const Scenario& scenario) {
  const WorldSnapshot reference_snapshot = run_to_final_snapshot(scenario);
  require(reference_snapshot.tick_sequence().value() == scenario.ticks_per_sample,
          "BENCHMARK.REFERENCE_TICK_MISMATCH", "reference simulation ended at the wrong tick");
  require(reference_snapshot.players().size() == scenario.player_count,
          "BENCHMARK.REFERENCE_PLAYER_COUNT_MISMATCH",
          "reference simulation ended with the wrong player count");
  const std::string final_snapshot_hash = snapshot_hash(reference_snapshot);
  require(snapshot_hash(run_to_final_snapshot(scenario)) == final_snapshot_hash,
          "BENCHMARK.REFERENCE_SNAPSHOT_HASH_MISMATCH",
          "independent untimed simulations produced different final snapshots");

  const SimulationConfig configuration = make_configuration(scenario);
  const GameWorld world = make_world(scenario);
  const SpatialGrid reference_grid = SpatialGrid::create(configuration, world);
  const std::size_t candidate_pair_count = reference_grid.candidate_pairs().size();
  const std::string expected_candidate_hash = candidate_pair_hash(reference_grid);

  const protocol::RequestId request_id = protocol::RequestId::create("benchmark-request");
  const std::string reference_encoding =
      protocol::encode_snapshot_message(reference_snapshot, request_id, 1U, kProtocolTimestamp);
  const std::string expected_encoding_hash = byte_string_hash(reference_encoding);

  run_warmup(scenario, reference_snapshot);

  const Measurement simulation_steps = measure_simulation_steps(scenario, final_snapshot_hash);
  const Measurement grid_rebuilds =
      measure_grid_rebuilds(scenario, candidate_pair_count, expected_candidate_hash);
  const Measurement snapshots = measure_snapshot_creation(scenario, final_snapshot_hash);
  const Measurement encodings = measure_json_encoding(
      scenario, reference_snapshot, expected_encoding_hash, reference_encoding.size());

  json::object geometry;
  geometry.emplace("world_width", scenario.world_width);
  geometry.emplace("world_height", scenario.world_height);
  geometry.emplace("player_radius", scenario.player_radius);
  geometry.emplace("grid_columns", scenario.grid_columns);
  geometry.emplace("grid_rows", scenario.grid_rows);

  json::object density;
  density.emplace("layout", scenario.layout == Layout::kSparse ? "sparse" : "clustered");
  density.emplace("player_area_fraction", (static_cast<double>(scenario.player_count) * kPi *
                                           scenario.player_radius * scenario.player_radius) /
                                              (scenario.world_width * scenario.world_height));
  density.emplace("initial_candidate_pair_count", candidate_pair_count);

  json::object correctness;
  correctness.emplace("expected_final_tick_sequence", scenario.ticks_per_sample);
  correctness.emplace("expected_player_count", scenario.player_count);
  correctness.emplace("final_snapshot_hash", final_snapshot_hash);
  correctness.emplace("initial_candidate_pair_hash", expected_candidate_hash);
  correctness.emplace("encoded_snapshot_hash", expected_encoding_hash);
  correctness.emplace("encoded_snapshot_byte_count", reference_encoding.size());

  json::object measurements;
  measurements.emplace("simulation_step", encode_measurement(simulation_steps));
  measurements.emplace("grid_rebuild_and_candidate_pair_generation",
                       encode_measurement(grid_rebuilds));
  measurements.emplace("snapshot_creation", encode_measurement(snapshots));
  measurements.emplace("json_encoding", encode_measurement(encodings));

  json::object result;
  result.emplace("name", scenario.name);
  result.emplace("player_count", scenario.player_count);
  result.emplace("geometry", std::move(geometry));
  result.emplace("density", std::move(density));
  result.emplace("correctness", std::move(correctness));
  result.emplace("measurements", std::move(measurements));
  return result;
}

[[nodiscard]] json::object platform_metadata() {
  utsname names{};
  if (uname(&names) != 0) {
    fail("BENCHMARK.UNAME_FAILED", "uname failed");
  }
  require(std::string_view(names.sysname) == "Linux", "BENCHMARK.LINUX_REQUIRED",
          "benchmark execution requires Linux");
  require(std::string_view(names.machine) == "x86_64", "BENCHMARK.X86_64_REQUIRED",
          "benchmark execution requires x86_64");
  require(Clock::is_steady, "BENCHMARK.STEADY_CLOCK_REQUIRED",
          "std::chrono::steady_clock is not steady on this platform");
  const std::string toolchain_id = environment_value("BLOB_ROYALE_TOOLCHAIN_ID");
  const std::string base_image_digest = environment_value("BLOB_ROYALE_BASE_IMAGE_DIGEST");
  require(toolchain_id == kExpectedToolchainId, "BENCHMARK.TOOLCHAIN_ID_MISMATCH",
          "benchmark execution requires the pinned toolchain identity");
  require(base_image_digest == kExpectedBaseImageDigest, "BENCHMARK.BASE_IMAGE_DIGEST_MISMATCH",
          "benchmark execution requires the pinned base-image digest");
  const unsigned int logical_cpu_count = std::thread::hardware_concurrency();
  require(logical_cpu_count > 0U, "BENCHMARK.LOGICAL_CPU_COUNT_MISSING",
          "hardware concurrency is unavailable");

  json::object metadata;
  metadata.emplace("operating_system", names.sysname);
  metadata.emplace("architecture", names.machine);
  metadata.emplace("kernel_release", names.release);
  metadata.emplace("distribution", operating_system_name());
  metadata.emplace("cpu_model", cpu_model_name());
  metadata.emplace("logical_cpu_count", logical_cpu_count);
  metadata.emplace("physical_memory_bytes", physical_memory_bytes());
  metadata.emplace("cpu_governor", cpu_governor());
#if defined(__GLIBC__)
  metadata.emplace("glibc", gnu_get_libc_version());
#else
  metadata.emplace("glibc", "unavailable");
#endif
  metadata.emplace("compiler_id", BLOB_ROYALE_BENCHMARK_COMPILER_ID);
  metadata.emplace("compiler_version", BLOB_ROYALE_BENCHMARK_COMPILER_VERSION);
  metadata.emplace("compiler_version_string", __VERSION__);
  metadata.emplace("build_type", BLOB_ROYALE_BENCHMARK_BUILD_TYPE);
  metadata.emplace("boost", BOOST_LIB_VERSION);
  metadata.emplace("toolchain_id", toolchain_id);
  metadata.emplace("base_image_digest", base_image_digest);
  metadata.emplace("execution_environment", "pinned_oci_container");
  return metadata;
}

[[nodiscard]] json::object run_benchmarks(const RoyaleCaseInputs& royale_inputs) {
  constexpr Scenario scenarios[] = {
      {.name = "sparse_64",
       .player_count = 64,
       .layout = Layout::kSparse,
       .world_width = 2048.0,
       .world_height = 2048.0,
       .player_radius = 4.0,
       .grid_columns = 32,
       .grid_rows = 32,
       .ticks_per_sample = 400,
       .grid_rebuilds_per_sample = 200,
       .snapshots_per_sample = 500,
       .encodings_per_sample = 100},
      {.name = "sparse_512",
       .player_count = 512,
       .layout = Layout::kSparse,
       .world_width = 4096.0,
       .world_height = 4096.0,
       .player_radius = 4.0,
       .grid_columns = 64,
       .grid_rows = 64,
       .ticks_per_sample = 200,
       .grid_rebuilds_per_sample = 50,
       .snapshots_per_sample = 100,
       .encodings_per_sample = 20},
      {.name = "sparse_2048",
       .player_count = 2048,
       .layout = Layout::kSparse,
       .world_width = 8192.0,
       .world_height = 8192.0,
       .player_radius = 4.0,
       .grid_columns = 128,
       .grid_rows = 128,
       .ticks_per_sample = 80,
       .grid_rebuilds_per_sample = 20,
       .snapshots_per_sample = 25,
       .encodings_per_sample = 5},
      {.name = "clustered_512",
       .player_count = 512,
       .layout = Layout::kClustered,
       .world_width = 512.0,
       .world_height = 512.0,
       .player_radius = 4.0,
       .grid_columns = 16,
       .grid_rows = 16,
       .ticks_per_sample = 80,
       .grid_rebuilds_per_sample = 10,
       .snapshots_per_sample = 100,
       .encodings_per_sample = 20},
  };

  json::object platform = platform_metadata();

  json::object measurement_policy;
  measurement_policy.emplace("lens", "distributional robust summaries");
  measurement_policy.emplace("comparison_class", "advisory");
  measurement_policy.emplace("regression_budgets_enforced", false);
  measurement_policy.emplace("dedicated_native_linux_runner_required_for_budgets", true);
  measurement_policy.emplace("warmup_run_count", kWarmupRunCount);
  measurement_policy.emplace("timed_sample_count", kTimedSampleCount);
  measurement_policy.emplace("timed_regions_include_correctness_hashing", false);
  measurement_policy.emplace("delivery_backpressure_measured", true);

  const BackpressureOutcome backpressure = measure_delivery_backpressure();

  json::array case_results;
  case_results.reserve(std::size(scenarios));
  for (const Scenario& scenario : scenarios) {
    case_results.emplace_back(benchmark_scenario(scenario));
  }
  case_results.emplace_back(benchmark_royale_case(royale_inputs));

  json::object output;
  output.emplace("schema", kBenchmarkSchema);
  output.emplace("status", "passed");
  output.emplace("suite", "blob_simulation_benchmarks");
  output.emplace("measurement_policy", std::move(measurement_policy));
  output.emplace("platform", std::move(platform));
  output.emplace("cases", std::move(case_results));
  output.emplace("delivery_backpressure", encode_backpressure_outcome(backpressure));
  output.emplace("process_peak_resident_set_size_bytes", peak_resident_set_size_bytes());
  return output;
}

[[nodiscard]] json::object error_output(const std::string_view code,
                                        const std::string_view message) {
  json::object error;
  error.emplace("code", code);
  error.emplace("message", message);

  json::object output;
  output.emplace("schema", kBenchmarkSchema);
  output.emplace("status", "failed");
  output.emplace("error", std::move(error));
  return output;
}

} // namespace
} // namespace blob_royale::benchmarks

int main(const int argument_count, const char* const arguments[]) {
  namespace json = boost::json;
  using blob_royale::benchmarks::BenchmarkError;
  using blob_royale::benchmarks::error_output;
  using blob_royale::benchmarks::RoyaleCaseInputs;
  using blob_royale::benchmarks::run_benchmarks;

  try {
    // Exactly the two inputs the royale case reads, passed by `scripts/run-benchmarks-linux`.
    if (argument_count != 5 || std::string_view(arguments[1]) != "--deployment-config" ||
        std::string_view(arguments[3]) != "--maps-directory") {
      std::cerr << json::serialize(
                       error_output("BENCHMARK.ARGUMENTS_UNSUPPORTED",
                                    "usage: blob_simulation_benchmarks --deployment-config <path> "
                                    "--maps-directory <path>"))
                << '\n';
      return 64;
    }
    const RoyaleCaseInputs royale_inputs{.deployment_configuration = arguments[2],
                                         .maps_directory = arguments[4]};
    std::cout << json::serialize(run_benchmarks(royale_inputs)) << '\n';
    return 0;
  } catch (const BenchmarkError& error) {
    std::cerr << json::serialize(error_output(error.code(), error.what())) << '\n';
  } catch (const std::exception& error) {
    std::cerr << json::serialize(error_output("BENCHMARK.UNEXPECTED_FAILURE", error.what()))
              << '\n';
  } catch (...) {
    std::cerr << json::serialize(
                     error_output("BENCHMARK.NON_STANDARD_FAILURE", "non-standard exception"))
              << '\n';
  }
  return 1;
}
