#ifndef BLOB_ROYALE_SIMULATION_MAP_DEFINITION_HPP
#define BLOB_ROYALE_SIMULATION_MAP_DEFINITION_HPP

#include "physics_body.hpp"
#include "team_id.hpp"
#include "vector2.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

// canonical: arena_bounds -- the rectangular play area one map declares.
//
// The rectangle is anchored at the origin and spans `[0, width] x [0, height]`, which is exactly
// the geometry `docs/architecture/0003-deterministic-simulation-contract.md` § "Wall policy" folds
// against: one axis has permitted centre interval `[radius, extent - radius]`. Anchoring is part of
// the accepted physics rather than a simplification here -- moving the origin would change the
// fold, which is a versioned physics amendment and not a map property.
//
// A **non-rectangular arena is the named missing seam**: phase 4's fold is what guarantees a
// committed centre is in bounds for arbitrarily large finite overshoot, so a differently shaped
// arena is approximated with static bodies inside a rectangular bound
// (`docs/architecture/0004-gameplay-architecture.md` § "Justified extension points and what-if
// stress").
// related: map_definition -- the value that owns one of these.
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

// canonical: map_metadata -- one map's bounded, ordered key/value annotations.
//
// Metadata is authoring content the engine never interprets: a mode, a renderer, or a tool reads
// the keys it understands and ignores the rest. Entries are canonicalized to ascending key order
// and a duplicate key is a validation failure, so two maps that declare the same annotations
// compare equal whatever order they were authored in.
class MapMetadata final {
public:
  struct Entry final {
    std::string key;
    std::string value;

    friend bool operator==(const Entry&, const Entry&) = default;
  };

  // Canonicalizes to ascending key order. Throws SimulationValidationError for an empty or
  // oversized key or value, a key outside the accepted snake_case alphabet, a duplicate key, and
  // an entry count past kMaximumMapMetadataEntryCount.
  [[nodiscard]] static MapMetadata create(std::vector<Entry> entries);

  // The annotations of a map or marker that declares none.
  [[nodiscard]] static MapMetadata none();

  MapMetadata() = default;
  MapMetadata(const MapMetadata&) = default;
  MapMetadata(MapMetadata&&) noexcept = default;
  MapMetadata& operator=(const MapMetadata&) = default;
  MapMetadata& operator=(MapMetadata&&) noexcept = default;
  ~MapMetadata() = default;

  [[nodiscard]] std::span<const Entry> entries() const& noexcept { return entries_; }
  [[nodiscard]] std::span<const Entry> entries() const&& = delete;

  // The value of one key, or nullptr when the map does not declare it. Total: an unknown key is a
  // defined absence rather than a caller precondition.
  [[nodiscard]] const std::string* find(std::string_view key) const& noexcept;
  [[nodiscard]] const std::string* find(std::string_view key) const&& = delete;

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  friend bool operator==(const MapMetadata&, const MapMetadata&) = default;

private:
  explicit MapMetadata(std::vector<Entry> entries) noexcept;

  std::vector<Entry> entries_;
};

// canonical: map_definition -- the static, mode-independent content of one arena.
// @extension-point map_definition
//
// A map is content, not code: a validated value any mode can play if it provides what that mode's
// `validate_map` requires (`docs/architecture/0004-gameplay-architecture.md` § "Maps as data").
//
// **Markers are the one authoring concept.** `spawn_points()` is the ordered projection of the
// markers whose kind is `spawn`, materialized once at construction because every mode needs it; it
// is a convenience, never a second way to author a point. A mode reads the marker kinds it
// understands and ignores the rest, which is what lets any mode play any map, and a mode that
// *requires* a kind rejects the map at startup rather than discovering the absence mid-match.
//
// The map is the arena source for the kernel: phase 4's fold, the commit-time bounds validation,
// and the spatial index all read `bounds()`. SimulationConfig keeps `world_width` and
// `world_height` because protocol v1's `/api/v1/config` publishes them through
// `PublicConfiguration`, so the `[world]` INI keys stay beside `[match] map=` rather than retiring
// into the map file. The two cannot silently disagree:
// `src/application/match_startup_validation.hpp` rejects a map whose arena is not the rectangle
// `[world]` publishes, because the kernel folds against the map and every client draws the
// published scalars.
//
// **`static_bodies()` is declared content, not seated entities.** Seating them is
// `GameWorld::create(configuration, map, seed)`, which owns the id policy for map content and
// numbers a map's bodies `kMinimumEntityId + index` in declared order, so a map's entities are a
// deterministic function of the map file alone.
//
// Adding a map is adding a data directory and naming it in configuration -- no code at all:
//
//   new  maps/<name>/map.cfg            name, bounds, metadata
//   new  maps/<name>/static_bodies.csv  obstacles
//   new  maps/<name>/markers.csv        spawn points and mode props
//   edit match configuration            `[match] map=`
//
// related: physics_body.hpp -- the value a static body is.
// related: game_simulation.hpp -- the kernel that reads this map's arena every tick.
class MapDefinition final {
public:
  // The one marker kind the engine itself knows. Every other kind is a mode's vocabulary and the
  // engine neither validates nor interprets it.
  static constexpr std::string_view kSpawnMarkerKind = "spawn";

  struct Marker final {
    std::string kind; // snake_case, e.g. "spawn", "flag_home", "hill_center"
    Vector2 position;
    std::optional<TeamId> team; // absent means the marker is unaligned
    MapMetadata metadata;

    // Creates a validated marker or throws SimulationValidationError. The kind must be a non-empty
    // snake_case identity within kMaximumMapMarkerKindLength.
    [[nodiscard]] static Marker create(std::string kind, Vector2 position,
                                       std::optional<TeamId> team, MapMetadata metadata);

    // The unaligned spawn point, which is the marker every mode understands.
    [[nodiscard]] static Marker spawn(Vector2 position);

    friend bool operator==(const Marker&, const Marker&) = default;
  };

  // Creates a validated map or throws SimulationValidationError. Rejects an empty or oversized
  // name, a name outside the accepted alphabet, a body in `static_bodies` that is not static, a
  // static body or marker centre outside the closed arena rectangle, and a static body or marker
  // count past its accepted limit.
  //
  // A marker's fit for the configured disc radius is deliberately not checked here: a map is
  // mode- and configuration-independent content, and the radius belongs to SimulationConfig.
  // `require_spawn_points_are_seatable` in `spawn_system.hpp` is the one place they meet, and it
  // runs at construction so a mismatch is a startup rejection rather than a mid-match failure.
  [[nodiscard]] static MapDefinition create(std::string name, ArenaBounds bounds,
                                            std::vector<PhysicsBody> static_bodies,
                                            std::vector<Marker> markers, MapMetadata metadata);

  // The degenerate map: an empty rectangle with no static bodies, no markers, and no metadata.
  // This is the value `GameSimulation::create(configuration, world)` synthesizes so every caller
  // written before maps existed stays expressible, and it is the map ADR 0003's accepted fixtures
  // run on.
  [[nodiscard]] static MapDefinition bare_arena(ArenaBounds bounds);

  MapDefinition(const MapDefinition&) = default;
  MapDefinition(MapDefinition&&) noexcept = default;
  MapDefinition& operator=(const MapDefinition&) = default;
  MapDefinition& operator=(MapDefinition&&) noexcept = default;
  ~MapDefinition() = default;

  [[nodiscard]] std::string_view name() const& noexcept { return name_; }
  [[nodiscard]] std::string_view name() const&& = delete;

  [[nodiscard]] const ArenaBounds& bounds() const& noexcept { return bounds_; }
  [[nodiscard]] const ArenaBounds& bounds() const&& = delete;

  [[nodiscard]] std::span<const PhysicsBody> static_bodies() const& noexcept {
    return static_bodies_;
  }
  [[nodiscard]] std::span<const PhysicsBody> static_bodies() const&& = delete;

  // Every declared marker, in the order the map declared it.
  [[nodiscard]] std::span<const Marker> markers() const& noexcept { return markers_; }
  [[nodiscard]] std::span<const Marker> markers() const&& = delete;

  // The markers of kind `spawn`, in declared order. Derived, never separately authored.
  [[nodiscard]] std::span<const Marker> spawn_points() const& noexcept { return spawn_points_; }
  [[nodiscard]] std::span<const Marker> spawn_points() const&& = delete;

  [[nodiscard]] const MapMetadata& metadata() const& noexcept { return metadata_; }
  [[nodiscard]] const MapMetadata& metadata() const&& = delete;

  friend bool operator==(const MapDefinition&, const MapDefinition&) = default;

private:
  MapDefinition(std::string name, ArenaBounds bounds, std::vector<PhysicsBody> static_bodies,
                std::vector<Marker> markers, std::vector<Marker> spawn_points,
                MapMetadata metadata) noexcept;

  std::string name_;
  ArenaBounds bounds_;
  std::vector<PhysicsBody> static_bodies_;
  std::vector<Marker> markers_;
  // Materialized once, because every mode needs it and a per-tick filter would be a scan the
  // ordering contract does not need.
  std::vector<Marker> spawn_points_;
  MapMetadata metadata_;
};

} // namespace blob_royale::simulation

#endif
