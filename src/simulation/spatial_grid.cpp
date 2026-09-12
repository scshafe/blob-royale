#include "spatial_grid.hpp"
#include "motion_body_envelope.hpp"

#include "component_store.hpp"
#include "physics_body.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::simulation {
namespace {

struct CellCoverage final {
  std::size_t first_row;
  std::size_t last_row;
  std::size_t first_column;
  std::size_t last_column;
};

[[nodiscard]] std::size_t checked_cell_count(const SimulationConfig& configuration) {
  const std::size_t row_count = configuration.spatial_grid_rows();
  const std::size_t column_count = configuration.spatial_grid_columns();
  const std::size_t maximum_cell_count = SimulationConfig::kMaximumSpatialGridCellCount;
  if (row_count == 0 || column_count == 0 || row_count > maximum_cell_count / column_count) {
    throw SimulationValidationError(
        SimulationValidationCode::kSpatialGridCellCountExceeded, "spatial_grid.cells",
        "row count multiplied by column count exceeds the accepted allocation limit");
  }

  const std::size_t cell_count = row_count * column_count;
  if (cell_count > maximum_cell_count || cell_count != configuration.spatial_grid_cell_count()) {
    throw SimulationValidationError(
        SimulationValidationCode::kSpatialGridCellCountExceeded, "spatial_grid.cells",
        "validated grid cell count is inconsistent or exceeds the accepted allocation limit");
  }
  return cell_count;
}

void validate_cell_extent(const double world_extent, const std::size_t cell_count,
                          const std::string_view context) {
  const double extent = world_extent / static_cast<double>(cell_count);
  if (!std::isfinite(extent) || extent <= 0.0) {
    throw SimulationValidationError(SimulationValidationCode::kSpatialGridGeometryInvalid,
                                    std::string(context),
                                    "derived cell extent must be finite and greater than zero");
  }
}

[[nodiscard]] double internal_boundary(const double world_extent, const std::size_t cell_count,
                                       const std::size_t boundary_index) noexcept {
  return (world_extent * static_cast<double>(boundary_index)) / static_cast<double>(cell_count);
}

[[nodiscard]] std::size_t first_intersected_cell(const double minimum, const double world_extent,
                                                 const std::size_t cell_count) noexcept {
  if (minimum <= 0.0 || cell_count == 1) {
    return 0;
  }

  std::size_t first = 1;
  std::size_t last = cell_count;
  while (first < last) {
    const std::size_t middle = first + ((last - first) / 2);
    if (internal_boundary(world_extent, cell_count, middle) < minimum) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  return first == cell_count ? cell_count - 1 : first - 1;
}

[[nodiscard]] std::size_t last_intersected_cell(const double maximum, const double world_extent,
                                                const std::size_t cell_count) noexcept {
  if (maximum >= world_extent || cell_count == 1) {
    return cell_count - 1;
  }

  std::size_t first = 1;
  std::size_t last = cell_count;
  while (first < last) {
    const std::size_t middle = first + ((last - first) / 2);
    if (internal_boundary(world_extent, cell_count, middle) <= maximum) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  return first - 1;
}

// A body's coverage over the map's arena. Admission uses the canonical motion-envelope guard:
// static centers must lie in the closed rectangle (including static bodies marked crossing);
// dynamic folding centers also lie in that rectangle, their effective diameter must fit, and a
// zero-span axis requires zero velocity. An initially wall-overlapping disc is legal and is not
// snapped into the radius inset. Both static and dynamic bodies are indexed.
//
// **A dynamic crossing body is exempt from the envelope and is still indexed, clamped to the edge
// cells it is nearest.** The grid has no cell for a point outside the arena, so the choice is
// between leaving such a body out of the index while it is outside and clamping its coverage.
// Clamping is taken, and the reason is the broad-phase guarantee itself: ADR 0003 § "Spatial-grid
// policy and partition boundaries" requires the index to be a *superset* of the pairs whose
// committed discs can touch, and a hazard whose centre is one step outside the wall has a disc that
// already overlaps a blob just inside it. An absent body would make that a missed contact rather
// than a deferred one, which is a hole in the narrow phase's input and not merely a scheduling
// delay. The clamps below already produce this: a coverage box that lies entirely outside collapses
// to the edge row or column on that side, and one that straddles the edge keeps its interior part,
// so two nearby bodies land in a shared cell whether they are inside, outside, or one of each.
//
// **What it costs.** A body far outside the arena is a member of the edge cells it clamps to, so it
// is offered as a candidate against everything else in those cells and counts against the
// membership and candidate-pair limits, even though the narrow phase then rejects every one of
// those pairs on distance. That is bounded work proportional to the edge cells' population and it
// buys back the superset guarantee; the alternative bought a little work and sold a contact.
//
// Coverage and folding-envelope fit both use the declared radius, falling back to the configured
// radius only when it is undeclared. Coverage answers which cells the complete disc can touch;
// admission asks whether its center and effective diameter form a legal motion envelope. They do
// not impose the same positional interval. The conservative coverage/padding arithmetic below is
// unchanged by the Step 16 promotion of the previously proved admission guard.
[[nodiscard]] CellCoverage body_coverage(const SimulationConfig& configuration,
                                         const ArenaBounds& bounds,
                                         const ComponentStore<PhysicsBody>::Entry& body_entry) {
  const Vector2& position = body_entry.value.position();
  const double configured_radius = configuration.player_radius();
  const double coverage_radius = effective_radius(body_entry.value, configured_radius);
  const auto violation =
      motion_body_envelope_violation(body_entry.value, bounds, configured_radius);
  if (violation) {
    const bool is_static = *violation == MotionBodyEnvelopeViolation::kStaticOutsideEnvelope;
    throw SimulationValidationError(
        is_static ? SimulationValidationCode::kSpatialGridStaticBodyOutOfBounds
                  : SimulationValidationCode::kSpatialGridPlayerCenterOutOfBounds,
        "spatial_grid.bodies[entity_id=" + std::to_string(body_entry.entity.value()) + "].position",
        is_static ? "static body center must lie inside the closed arena rectangle"
                  : "folding body must fit its motion envelope");
  }

  const double world_width = bounds.width();
  const double world_height = bounds.height();
  // Solve d <= 2r + epsilon + relative*d for the greatest accepted excess beyond 2r. Expanding
  // each AABB by that conservative amount guarantees the grid remains a broad-phase superset of
  // the narrow phase. `nextafter` retains the guarantee when the padding is below a local ULP.
  //
  // The padding is derived from this one body's own `2 * r`, which stays conservative for unequal
  // radii: the slack a pair needs at the acceptance boundary is about
  // `epsilon + relative * (r_a + r_b)`, and the two bodies contribute
  // `(2 * epsilon + relative * 2 * (r_a + r_b)) / (1 - relative)` between them, which is larger.
  const double contact_distance = 2.0 * coverage_radius;
  const double coverage_padding =
      comparison_tolerance(contact_distance, contact_distance, kPositionTolerance) /
      (1.0 - kRelativeTolerance);
  const double minimum_x =
      std::max(0.0, std::nextafter(position.x() - coverage_radius - coverage_padding,
                                   -std::numeric_limits<double>::infinity()));
  const double maximum_x =
      std::min(world_width, std::nextafter(position.x() + coverage_radius + coverage_padding,
                                           std::numeric_limits<double>::infinity()));
  const double minimum_y =
      std::max(0.0, std::nextafter(position.y() - coverage_radius - coverage_padding,
                                   -std::numeric_limits<double>::infinity()));
  const double maximum_y =
      std::min(world_height, std::nextafter(position.y() + coverage_radius + coverage_padding,
                                            std::numeric_limits<double>::infinity()));

  return CellCoverage{
      first_intersected_cell(minimum_y, world_height, configuration.spatial_grid_rows()),
      last_intersected_cell(maximum_y, world_height, configuration.spatial_grid_rows()),
      first_intersected_cell(minimum_x, world_width, configuration.spatial_grid_columns()),
      last_intersected_cell(maximum_x, world_width, configuration.spatial_grid_columns())};
}

[[nodiscard]] std::size_t coverage_entry_count(const CellCoverage& coverage) {
  const std::size_t row_count = coverage.last_row - coverage.first_row + 1;
  const std::size_t column_count = coverage.last_column - coverage.first_column + 1;
  if (row_count > kMaximumSpatialGridMembershipCount / column_count) {
    throw SimulationValidationError(
        SimulationValidationCode::kSpatialGridMembershipLimitExceeded, "spatial_grid.memberships",
        "one player coverage exceeds the accepted membership allocation limit");
  }
  return row_count * column_count;
}

[[nodiscard]] std::size_t flattened_index(const std::size_t row, const std::size_t column_count,
                                          const std::size_t column) noexcept {
  return (row * column_count) + column;
}

void validate_candidate_observation_count(const std::vector<std::size_t>& member_counts) {
  std::size_t observation_count = 0;
  for (const std::size_t member_count : member_counts) {
    if (member_count < 2) {
      continue;
    }
    if (member_count > std::numeric_limits<std::size_t>::max() / (member_count - 1)) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pair_observations",
          "one cell candidate traversal cannot be represented safely");
    }
    const std::size_t cell_observation_count = (member_count * (member_count - 1)) / 2;
    if (cell_observation_count >
        kMaximumSpatialGridCandidatePairObservationCount - observation_count) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pair_observations",
          "cell candidate traversal exceeds the accepted deterministic work limit");
    }
    observation_count += cell_observation_count;
  }
}

[[nodiscard]] std::vector<std::vector<EntityId>>
build_cells(const GameWorld& world, const std::vector<CellCoverage>& coverages,
            const std::size_t column_count, const std::size_t cell_count) {
  std::vector<std::size_t> member_counts(cell_count, 0);
  for (const CellCoverage& coverage : coverages) {
    for (std::size_t row = coverage.first_row; row <= coverage.last_row; ++row) {
      for (std::size_t column = coverage.first_column; column <= coverage.last_column; ++column) {
        ++member_counts[flattened_index(row, column_count, column)];
      }
    }
  }
  validate_candidate_observation_count(member_counts);

  std::vector<std::vector<EntityId>> cells(cell_count);
  for (std::size_t cell_index = 0; cell_index < cell_count; ++cell_index) {
    cells[cell_index].reserve(member_counts[cell_index]);
  }

  const std::span<const ComponentStore<PhysicsBody>::Entry> bodies =
      world.store<PhysicsBody>().entries();
  for (std::size_t body_index = 0; body_index < bodies.size(); ++body_index) {
    const CellCoverage& coverage = coverages[body_index];
    for (std::size_t row = coverage.first_row; row <= coverage.last_row; ++row) {
      for (std::size_t column = coverage.first_column; column <= coverage.last_column; ++column) {
        cells[flattened_index(row, column_count, column)].push_back(bodies[body_index].entity);
      }
    }
  }
  return cells;
}

[[nodiscard]] std::size_t maximum_pair_count(const std::size_t player_count) {
  if (player_count < 2) {
    return 0;
  }
  if (player_count > kMaximumEntityCount ||
      player_count > std::numeric_limits<std::size_t>::max() / (player_count - 1)) {
    throw SimulationValidationError(
        SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
        "spatial_grid.candidate_pairs",
        "player count cannot be represented by the bounded candidate-pair index");
  }

  const std::size_t pair_count = (player_count * (player_count - 1)) / 2;
  if (pair_count > kMaximumSpatialGridCandidatePairCount) {
    throw SimulationValidationError(
        SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
        "spatial_grid.candidate_pairs",
        "maximum candidate pair count exceeds the accepted allocation limit");
  }
  return pair_count;
}

[[nodiscard]] std::size_t
body_index_for_id(const std::span<const ComponentStore<PhysicsBody>::Entry> bodies,
                  const EntityId id) noexcept {
  const auto match =
      std::lower_bound(bodies.begin(), bodies.end(), id,
                       [](const ComponentStore<PhysicsBody>::Entry& body_entry,
                          const EntityId searched_id) { return body_entry.entity < searched_id; });
  return static_cast<std::size_t>(match - bodies.begin());
}

// A packed triangular bitset deduplicates cell observations before any pair value is stored. The
// companion vector therefore contains at most the preflighted unique-pair bound, never duplicates.
class CandidatePairAccumulator final {
public:
  explicit CandidatePairAccumulator(const std::size_t player_count)
      : player_count_(player_count), pair_count_limit_(checked_pair_storage_count(player_count)),
        observed_pair_bits_(checked_bitset_byte_count(pair_count_limit_), 0) {}

  void observe(const std::size_t lower_index, const std::size_t higher_index,
               const EntityId lower_id, const EntityId higher_id) {
    const std::size_t bit_index = pair_bit_index(lower_index, higher_index);
    const std::size_t byte_index = bit_index / 8;
    const auto bit_mask = static_cast<std::uint8_t>(1U << (bit_index % 8));
    if ((observed_pair_bits_[byte_index] & bit_mask) == 0U) {
      observed_pair_bits_[byte_index] |= bit_mask;
      reserve_for_next_candidate();
      candidate_pairs_.push_back(CandidatePair::create(lower_id, higher_id));
    }
  }

  [[nodiscard]] std::vector<CandidatePair> materialize() && {
    std::sort(candidate_pairs_.begin(), candidate_pairs_.end());
    return std::move(candidate_pairs_);
  }

private:
  [[nodiscard]] static std::size_t checked_pair_storage_count(const std::size_t player_count) {
    const std::size_t pair_count = maximum_pair_count(player_count);
    if (pair_count > kMaximumSpatialGridCandidatePairAllocationByteCount / sizeof(CandidatePair)) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pairs",
          "maximum candidate-pair storage exceeds the accepted byte allocation limit");
    }
    return pair_count;
  }

  [[nodiscard]] static std::size_t checked_bitset_byte_count(const std::size_t pair_count_limit) {
    if (pair_count_limit > std::numeric_limits<std::size_t>::max() - 7) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pairs.bitset",
          "candidate-pair bitset byte count cannot be represented safely");
    }
    const std::size_t byte_count = (pair_count_limit + 7) / 8;
    if (byte_count > kMaximumSpatialGridCandidatePairBitsetByteCount) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pairs.bitset",
          "candidate-pair bitset exceeds the accepted allocation limit");
    }
    return byte_count;
  }

  [[nodiscard]] std::size_t pair_bit_index(const std::size_t lower_index,
                                           const std::size_t higher_index) const noexcept {
    const std::size_t preceding_pair_count =
        (lower_index * ((2 * player_count_) - lower_index - 1)) / 2;
    return preceding_pair_count + (higher_index - lower_index - 1);
  }

  void reserve_for_next_candidate() {
    if (candidate_pairs_.size() < candidate_pairs_.capacity()) {
      return;
    }
    if (candidate_pairs_.size() == pair_count_limit_) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded,
          "spatial_grid.candidate_pairs",
          "observed candidates exceed the preflighted pair allocation bound");
    }

    constexpr std::size_t initial_capacity = 64;
    const std::size_t current_capacity = candidate_pairs_.capacity();
    const std::size_t next_capacity =
        current_capacity == 0
            ? std::min(initial_capacity, pair_count_limit_)
            : (current_capacity > pair_count_limit_ / 2 ? pair_count_limit_ : current_capacity * 2);
    candidate_pairs_.reserve(next_capacity);
  }

  std::size_t player_count_;
  std::size_t pair_count_limit_;
  std::vector<std::uint8_t> observed_pair_bits_;
  std::vector<CandidatePair> candidate_pairs_;
};

[[nodiscard]] std::vector<CandidatePair>
build_candidate_pairs(const GameWorld& world, const std::vector<std::vector<EntityId>>& cells) {
  const std::span<const ComponentStore<PhysicsBody>::Entry> bodies =
      world.store<PhysicsBody>().entries();
  CandidatePairAccumulator candidate_pair_accumulator(bodies.size());
  std::vector<std::size_t> cell_player_indices;
  cell_player_indices.reserve(bodies.size());
  for (const std::vector<EntityId>& cell : cells) {
    cell_player_indices.clear();
    for (const EntityId id : cell) {
      cell_player_indices.push_back(body_index_for_id(bodies, id));
    }

    for (std::size_t left_cell_index = 0; left_cell_index < cell_player_indices.size();
         ++left_cell_index) {
      for (std::size_t right_cell_index = left_cell_index + 1;
           right_cell_index < cell_player_indices.size(); ++right_cell_index) {
        candidate_pair_accumulator.observe(cell_player_indices[left_cell_index],
                                           cell_player_indices[right_cell_index],
                                           cell[left_cell_index], cell[right_cell_index]);
      }
    }
  }
  return std::move(candidate_pair_accumulator).materialize();
}

} // namespace

SpatialGrid SpatialGrid::create(SimulationConfig configuration, const ArenaBounds bounds,
                                const GameWorld& world) {
  const std::size_t cell_count = checked_cell_count(configuration);
  validate_cell_extent(bounds.width(), configuration.spatial_grid_columns(),
                       "spatial_grid.cell_width");
  validate_cell_extent(bounds.height(), configuration.spatial_grid_rows(),
                       "spatial_grid.cell_height");

  std::vector<CellCoverage> coverages;
  coverages.reserve(world.store<PhysicsBody>().size());
  std::size_t membership_count = 0;
  for (const ComponentStore<PhysicsBody>::Entry& body_entry :
       world.store<PhysicsBody>().entries()) {
    const CellCoverage coverage = body_coverage(configuration, bounds, body_entry);
    const std::size_t player_membership_count = coverage_entry_count(coverage);
    if (player_membership_count > kMaximumSpatialGridMembershipCount - membership_count) {
      throw SimulationValidationError(
          SimulationValidationCode::kSpatialGridMembershipLimitExceeded, "spatial_grid.memberships",
          "total cell membership exceeds the accepted allocation limit");
    }
    membership_count += player_membership_count;
    coverages.push_back(coverage);
  }

  std::vector<Cell> cells =
      build_cells(world, coverages, configuration.spatial_grid_columns(), cell_count);
  std::vector<CandidatePair> candidate_pairs = build_candidate_pairs(world, cells);
  return SpatialGrid(configuration, bounds, std::move(cells), std::move(candidate_pairs));
}

SpatialGrid SpatialGrid::create(SimulationConfig configuration, const GameWorld& world) {
  const ArenaBounds bounds =
      ArenaBounds::create(configuration.world_width(), configuration.world_height());
  return create(std::move(configuration), bounds, world);
}

CellCoord SpatialGrid::home_cell(const Vector2& point) const {
  if (!bounds_.contains(point)) {
    throw SimulationValidationError(SimulationValidationCode::kSpatialGridPointOutOfBounds,
                                    "spatial_grid.home_cell.point",
                                    "point must lie inside the closed world rectangle");
  }

  return CellCoord::create(
      last_intersected_cell(point.y(), bounds_.height(), configuration_.spatial_grid_rows()),
      last_intersected_cell(point.x(), bounds_.width(), configuration_.spatial_grid_columns()));
}

std::span<const EntityId> SpatialGrid::cell_members(const CellCoord coordinate) const& {
  if (coordinate.row() >= row_count() || coordinate.column() >= column_count()) {
    throw SimulationValidationError(SimulationValidationCode::kSpatialGridCellCoordinateOutOfBounds,
                                    "spatial_grid.cell_members.coordinate",
                                    "row and column must identify a cell inside this grid");
  }
  return cells_[(coordinate.row() * column_count()) + coordinate.column()];
}

SpatialGrid SpatialGrid::rebuilt(const GameWorld& world) const& {
  return create(configuration_, bounds_, world);
}

SpatialGrid::SpatialGrid(SimulationConfig configuration, const ArenaBounds bounds,
                         std::vector<Cell> cells,
                         std::vector<CandidatePair> candidate_pairs) noexcept
    : configuration_(configuration), bounds_(bounds), cells_(std::move(cells)),
      candidate_pairs_(std::move(candidate_pairs)) {}

} // namespace blob_royale::simulation
