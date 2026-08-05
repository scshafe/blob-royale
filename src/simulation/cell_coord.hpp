#ifndef BLOB_ROYALE_SIMULATION_CELL_COORD_HPP
#define BLOB_ROYALE_SIMULATION_CELL_COORD_HPP

#include <compare>
#include <cstddef>

namespace blob_royale::simulation {

// canonical: spatial_grid_cell_coord -- row-major coordinates for simulation grid cells.
class CellCoord final {
public:
  // Creates a row-major coordinate. Grid-specific bounds are checked when it is queried.
  [[nodiscard]] static constexpr CellCoord create(const std::size_t row,
                                                  const std::size_t column) noexcept {
    return CellCoord(row, column);
  }

  CellCoord(const CellCoord&) = default;
  CellCoord(CellCoord&&) noexcept = default;
  CellCoord& operator=(const CellCoord&) = default;
  CellCoord& operator=(CellCoord&&) noexcept = default;
  ~CellCoord() = default;

  [[nodiscard]] constexpr std::size_t row() const noexcept { return row_; }
  [[nodiscard]] constexpr std::size_t column() const noexcept { return column_; }

  friend bool operator==(const CellCoord&, const CellCoord&) = default;
  friend std::strong_ordering operator<=>(const CellCoord&, const CellCoord&) = default;

private:
  constexpr CellCoord(const std::size_t row, const std::size_t column) noexcept
      : row_(row), column_(column) {}

  // Declaration order intentionally defines the canonical row-major comparison.
  std::size_t row_;
  std::size_t column_;
};

} // namespace blob_royale::simulation

#endif
