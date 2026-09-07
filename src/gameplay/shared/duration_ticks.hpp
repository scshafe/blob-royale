#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_DURATION_TICKS_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_DURATION_TICKS_HPP

#include <cstdint>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: duration_ticks -- the one conversion from a configured duration to tick counts.
//
// `ticks(x) = nearest integer to (x * 400), ties away from zero`, which is exactly `std::round` of
// the product (`docs/architecture/0005-royale-mode.md` § "Mode configuration"). **Durations are
// converted once, at load.** A validated configuration value stores only tick counts, no system
// sees a value in seconds, and nothing multiplies by the tick rate at runtime, which is what keeps
// a clock out of the simulation (`docs/architecture/0004-gameplay-architecture.md`
// § "Determinism obligations for framework code").
//
// **It lives in `shared/` because it now has two customers.** It was written in
// `royale/royale_configuration.cpp` with the note that "the rule of `src/gameplay/README.md` moves
// it to `shared/` the day a second mode declares one", and `shared/hazard_archetype.hpp` is that
// second customer: a mechanic in `shared/` may not include `royale/`, so the promotion is forced
// rather than optional. Two things changed in the move and nothing else did. The parameter is now
// the **full** configuration context (`royale.zone_shrink_seconds`,
// `hazard.comet.spawn_interval_seconds`) rather than a bare key this function prefixed with
// `royale.`, because a shared helper cannot know whose duration it is holding; and the three
// rejections carry `GAMEPLAY.DURATION_*` codes rather than `GAMEPLAY.ROYALE_*` ones, because a
// hazard's interval failing a royale-named rule would be a diagnostic that names the wrong owner.
// The arithmetic is unchanged, so every accepted tick count is the same value it was.
//
// Throws GameplayValidationError naming `configuration_context` for a non-finite duration, a
// negative one, and a tick count that does not fit the tick counter.
// related: royale/royale_configuration.hpp -- the four durations `[royale]` authors in seconds.
// related: shared/hazard_archetype.hpp -- the per-kind spawn interval a hazard authors in seconds.
// related: gameplay_validation_error.hpp -- the `GAMEPLAY.DURATION_*` rejections.
[[nodiscard]] std::uint64_t duration_ticks(double seconds, std::string_view configuration_context);

} // namespace blob_royale::gameplay

#endif
