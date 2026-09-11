#ifndef BLOB_ROYALE_TESTING_RANDOM_STREAM_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_RANDOM_STREAM_FIXTURE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace blob_royale::testing::random_stream_fixture {

inline constexpr std::array<std::uint64_t, 6> kMatchSeeds{
    0, 1, 3, 2026, 0x8000'0000'0000'0000ULL, std::numeric_limits<std::uint64_t>::max()};
inline constexpr std::uint64_t kOwnedSeed = 2026;
inline constexpr std::uint64_t kDifferentSeed = 2027;
inline constexpr std::size_t kMixedOperationCycles = 8;
inline constexpr std::size_t kIndependentDrawCount = 24;
inline constexpr std::uint64_t kRejectionSeed = 3;
inline constexpr std::uint64_t kRejectionBound = 0x8000'0000'0000'0001ULL;
inline constexpr std::uint64_t kRejectionResult = 0x3346'6F8A'7B81'A988ULL;
inline constexpr std::uint64_t kRejectionDrawCount = 2;

enum class DrawOperation { kBits, kUnitInterval, kBelow };

struct Draw final {
  DrawOperation operation;
  std::uint64_t bound{};
};

// Mixed operations exercise the written unit conversion and unbiased rejection sampler as well
// as raw bits. Large bounds force rejection in the corpus; the seed-3 specimen pins one exactly.
inline constexpr std::array kDraws{
    Draw{DrawOperation::kBits},
    Draw{DrawOperation::kUnitInterval},
    Draw{DrawOperation::kBelow, 1},
    Draw{DrawOperation::kBelow, 7},
    Draw{DrawOperation::kBelow, kRejectionBound},
    Draw{DrawOperation::kBelow, std::numeric_limits<std::uint64_t>::max()},
    Draw{DrawOperation::kUnitInterval},
    Draw{DrawOperation::kBelow, kRejectionBound}};

struct MixingGolden final {
  std::uint64_t input;
  std::uint64_t output;
};

inline constexpr std::array kMixingGoldens{
    MixingGolden{0, 0}, MixingGolden{1, 0x5692'161D'100B'05E5ULL},
    MixingGolden{0x9E37'79B9'7F4A'7C15ULL, 0xE220'A839'7B1D'CDAFULL},
    MixingGolden{0xFFFF'FFFF'FFFF'FFFFULL, 0xB4D0'55FC'F2CB'BD7BULL}};

struct HillGolden final {
  std::uint64_t match_seed;
  std::uint64_t hill_seed;
  std::array<std::uint64_t, 3> draws;
};

// Literal modulo-2^64 SplitMix64 results, calculated independently of production code. The last
// match seed specifically wraps the seed-plus-gamma addition; none are obtained from live RNGs.
inline constexpr std::array kHillGoldens{
    HillGolden{0, 0xE220'A839'7B1D'CDAFULL,
               {0xA706'DD2F'4D19'7E6FULL, 0xB382'A305'F441'4F5EULL, 0x631A'9154'FBAB'F717ULL}},
    HillGolden{1, 0x910A'2DEC'8902'5CC1ULL,
               {0x5E41'AB08'7439'611EULL, 0xF18D'6CE9'3D6C'F1EEULL, 0x0B95'F66D'327E'8D78ULL}},
    HillGolden{2026, 0xDB9C'5598'9194'8D23ULL,
               {0x6E75'7253'23D4'929EULL, 0x07B3'89BA'CFD8'F970ULL, 0x2926'7FA0'40AE'73FFULL}},
    HillGolden{0xFFFF'FFFF'FFFF'FFFFULL, 0xE4D9'7177'1B65'2C20ULL,
               {0x5DC2'0AA7'B2A2'7137ULL, 0xBDA5'668A'01D7'049CULL, 0x82B4'3276'ABB8'0226ULL}}};

struct InvalidKind final {
  std::uint8_t ordinal;
  std::string_view detail;
  std::string_view message;
};

inline constexpr std::array kInvalidKinds{
    InvalidKind{2, "random stream ordinal 2 is not registered",
                "SIMULATION.RANDOM_STREAM_KIND_INVALID [random_stream.kind]: random stream "
                "ordinal 2 is not registered"},
    InvalidKind{255, "random stream ordinal 255 is not registered",
                "SIMULATION.RANDOM_STREAM_KIND_INVALID [random_stream.kind]: random stream "
                "ordinal 255 is not registered"}};

} // namespace blob_royale::testing::random_stream_fixture

#endif
