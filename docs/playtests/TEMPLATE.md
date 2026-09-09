# Playtest, YYYY-MM-DD

Copy this file to `docs/playtests/<date>.md` and fill it in during or straight after the session.
It exists so a playtest produces a durable record rather than a memory, and so the balance numbers
that get changed afterwards have a reason attached to them.

## What was running

| Fact | Value |
|---|---|
| Deployed commit | `<sha>` (`docker inspect blob-royale --format '{{index .Config.Labels "blob-royale.deployed-commit"}}'`) |
| Release id | `<release-...>` (`grep release_id out/release/current/publication.env`) |
| Mode / map | |
| Bots | |
| Lobby minimum | |
| Drag / thrust maximum | |
| Zone shrink / minimum radius | |
| Elimination grace | |

## Who played

| Player | Device and browser | Notes |
|---|---|---|
| | | |

## Did a match complete?

- Winner or draw:
- Roughly how long from lobby to a decided match:
- Did the lobby wait correctly for both people, or did a match start without someone:

## How it felt

Answer in sentences, not scores. The point is to capture the things that only show up when a person
actually plays.

- **Steering.** Does thrust feel responsive or floaty? Is the terminal speed too fast or too slow to
  cross the arena? Deployed drag and thrust set that speed; both are one line in the config.
- **Collisions.** Does bumping another blob read as intentional and fair, or surprising?
- **The zone.** Is the shrink legible? Do you know where to go, and does the pressure arrive too
  early or too late?
- **Elimination.** Is it clear why you died? Is the grace period noticeable?
- **The bot.** Does the wanderer read as a player or as scenery? Would a chaser be better company?
- **Latency.** Does your own blob feel attached to your keys, or lagged? Note the device, since this
  is a tailnet round trip with no client-side prediction by design.

## What broke

Anything visibly wrong, with what you were doing at the time. A screenshot beats a description.
If the page recovered on its own, say so, because bounded reconnect is meant to be invisible.

## Balance changes to try

The one that decides who a match waits for is `[royale] lobby_seat_count`, the number of seats the
lobby is created with; a match starts when every seat is filled and somebody presses Start. Feel is mostly `drag_per_second`, the thrust maximum, and
the zone shrink. Apply a change in seconds with `./scripts/reconfigure-tailnet` on the host, then
commit whichever value you keep so the running arena and the repository agree.

| Value | From | To | Why |
|---|---|---|---|
| | | | |

## Follow-ups

Defects and ideas worth a plan step of their own. Do not fix balance or feel inline during the
playtest; write it here and decide afterwards.

- [ ]
