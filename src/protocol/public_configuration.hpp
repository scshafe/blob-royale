#ifndef BLOB_ROYALE_PROTOCOL_PUBLIC_CONFIGURATION_HPP
#define BLOB_ROYALE_PROTOCOL_PUBLIC_CONFIGURATION_HPP

#include <cstdint>

namespace blob_royale::protocol {

// canonical: public_configuration -- the complete immutable configuration exposed by protocol v1.
class PublicConfiguration final {
public:
  // Validates schema bounds and world/player cross-field invariants.
  // Throws ProtocolEncodingError with PUBLIC_CONFIGURATION_INVALID on failure.
  [[nodiscard]] static PublicConfiguration create(double world_width, double world_height,
                                                  double player_radius,
                                                  std::uint64_t snapshots_per_second);

  PublicConfiguration(const PublicConfiguration&) = default;
  PublicConfiguration(PublicConfiguration&&) noexcept = default;
  PublicConfiguration& operator=(const PublicConfiguration&) = default;
  PublicConfiguration& operator=(PublicConfiguration&&) noexcept = default;
  ~PublicConfiguration() = default;

  [[nodiscard]] double world_width() const noexcept { return world_width_; }
  [[nodiscard]] double world_height() const noexcept { return world_height_; }
  [[nodiscard]] double player_radius() const noexcept { return player_radius_; }
  [[nodiscard]] std::uint64_t snapshots_per_second() const noexcept {
    return snapshots_per_second_;
  }

  friend bool operator==(const PublicConfiguration&, const PublicConfiguration&) = default;

private:
  PublicConfiguration(double world_width, double world_height, double player_radius,
                      std::uint64_t snapshots_per_second) noexcept;

  double world_width_;
  double world_height_;
  double player_radius_;
  std::uint64_t snapshots_per_second_;
};

} // namespace blob_royale::protocol

#endif
