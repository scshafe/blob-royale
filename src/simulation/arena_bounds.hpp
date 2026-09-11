#ifndef BLOB_ROYALE_SIMULATION_ARENA_BOUNDS_HPP
#define BLOB_ROYALE_SIMULATION_ARENA_BOUNDS_HPP

#include "vector2.hpp"

namespace blob_royale::simulation {

// canonical: arena_bounds -- the rectangular envelope one terrain definition declares.
// The rectangle remains anchored at the origin, `[0, width] x [0, height]`. Terrain support
// inside this envelope does not alter the accepted live wall-fold arithmetic before Step 16.
// related: terrain_definition.hpp -- the sole authored owner of these bounds.
class ArenaBounds final {
public:
  // Creates validated bounds or throws SimulationValidationError. Width and height must be finite
  // and greater than zero, and at most kMaximumWorldDimension, which is the same rule
  // SimulationConfig applies to the world scalars it still publishes.
  [[nodiscard]] static ArenaBounds create(double width, double height);

  ArenaBounds(const ArenaBounds&) = default;
  ArenaBounds(ArenaBounds&&) noexcept = default;
  ArenaBounds& operator=(const ArenaBounds&) = default;
  ArenaBounds& operator=(ArenaBounds&&) noexcept = default;
  ~ArenaBounds() = default;

  [[nodiscard]] double width() const noexcept { return width_; }
  [[nodiscard]] double height() const noexcept { return height_; }

  // Whether a point lies in the closed rectangle. This is the rule a **static** body's centre
  // obeys: a wall legitimately sits on or past the centre interval a moving disc is held inside.
  [[nodiscard]] bool contains(const Vector2& point) const noexcept;

  // Whether a point is a legal centre for a disc of this radius, that is whether the complete
  // closed disc stays inside the rectangle. This is the rule a **dynamic** body's centre obeys and
  // is the interval phase 4 folds into.
  [[nodiscard]] bool contains_disc_center(const Vector2& point, double radius) const noexcept;

  friend bool operator==(const ArenaBounds&, const ArenaBounds&) = default;

private:
  ArenaBounds(double width, double height) noexcept;

  double width_;
  double height_;
};

} // namespace blob_royale::simulation

#endif
