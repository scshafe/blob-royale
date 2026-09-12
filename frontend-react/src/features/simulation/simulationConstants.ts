export const CONFIGURATION_RESPONSE_MAX_BYTES = 65_536;
export const CONFIGURATION_ENDPOINT_PATH = '/api/v1/config';

// Protocol v3 rooms. The directory is the one HTTP read v3 offers, and a client polls it only
// while it is choosing a room; a room's session target carries the one parametric segment v3 has,
// `[1-9][0-9]{0,2}`. Room one uses `/api/v3/lobbies/1/session`; there is no root session alias.
// The route and the subprotocol are one pair: the server refuses a v1 token on a session route, so
// neither value may be changed without the other.
export const LOBBY_DIRECTORY_ENDPOINT_PATH = '/api/v3/lobbies';
export const ROOM_SESSION_ENDPOINT_PATH_PREFIX = '/api/v3/lobbies/';
export const ROOM_SESSION_ENDPOINT_PATH_SUFFIX = '/session';
export const LOBBY_ID_PATTERN = /^[1-9][0-9]{0,2}$/;
export const SESSION_WEBSOCKET_SUBPROTOCOL = 'blob-royale.session.v3';
export const LOBBY_DIRECTORY_RESPONSE_MAX_BYTES = 65_536;
export const LOBBY_DIRECTORY_FETCH_TIMEOUT_MILLISECONDS = 10_000;
// A point-in-time read, refreshed once a second while the directory is on screen and not at all
// while it is not: admission is the decision, and the directory is advice about where to ask.
export const LOBBY_DIRECTORY_POLL_MILLISECONDS = 1_000;
export const SESSION_FRAME_MAX_BYTES = 2_097_152;
export const SESSION_ENTITY_LIMIT = 1_024;
export const COMMAND_MESSAGE_MAX_BYTES = 1_024;

export const RECONNECT_BACKOFF_MILLISECONDS = Object.freeze([
  1_000, 2_000, 4_000, 8_000, 16_000, 16_000,
] as const);
export const CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS = 10_000;
export const WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS = 10_000;

// A thrust is a level, not an impulse: it persists on the server until the next command, so the
// client sends only on change and never once per animation frame.
export const THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS = 50;
export const THRUST_GO_KEY_CODE = 'Space';
// The two ability bindings come out of the pool ADR 0008 already reserved: of Step 11a it records
// that "WASD/arrows no longer steer after Step 11a; those keys are available for later
// charge/shield bindings", and Step 11a froze exactly that pool as the fixtures' inert
// `REMOVED_DIRECTION_KEYS`. `KeyS` is the home-row key under the middle finger of the hand whose
// thumb holds the go key -- the fastest reachable code on the board, which belongs to the reactive
// move -- and `KeyD` is adjacent under the index finger, keeping the ADR's own S-for-shield,
// D-for-dash mnemonic. Both match on `event.code` exactly as the go key does, so the binding is
// layout-independent; the four arrow codes stay reserved and stay asserted inert.
//
// Shift was rejected on evidence rather than taste, and the alternative is recorded here because a
// reader would otherwise reach for it. The go-key guard deliberately filters `altKey`, `ctrlKey`
// and `metaKey` and deliberately does *not* filter `shiftKey` -- a choice an existing test pins, so
// Shift+Space thrusts today -- which means a bare-Shift shield would fire on the leading half of
// every Shift+Tab a keyboard user makes; five presses of it is the Windows Sticky Keys gesture; it
// is two codes (`ShiftLeft`/`ShiftRight`) against a one-constant-per-action model; and modifier
// keys do not auto-repeat, so the key-repeat rule ADR 0008 requires could only ever pass vacuously
// against it.
export const SHIELD_KEY_CODE = 'KeyS';
export const CHARGE_KEY_CODE = 'KeyD';
// A pulse is a one-shot, so it has no change-only gate to bound its rate the way a thrust level
// does, and this floor is the client's own side of the per-session command bucket: capacity 30,
// refill 20 per second, the token charged before parsing, and an empty bucket is a `1008
// command_rate_exceeded` close rather than a refusal. Held thrust with a moving cursor already runs
// at the full refill rate, so two unthrottled ability keys on top of it disconnect a player
// mid-match. A live published cooldown is the primary suppression; this is the backstop for the
// window between a press and the snapshot that would show that cooldown. The authored cooldowns are
// 0.9 s and 1.2 s, so a third of a second costs a player nothing they could have spent.
export const ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS = 300;
// A seat count is sent once, a quarter of a second after the last change: a dragged control emits
// a change per step, and thirty steps in a burst would spend the session's whole command bucket.
export const SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS = 250;

export const CANVAS_MAX_WIDTH_PIXELS = 960;
export const CANVAS_MAX_HEIGHT_PIXELS = 640;
// Logical pixels preserve a readable local scale independently of world dimensions. The bounded
// backing ratio limits allocation without changing the world extent or camera centre.
export const CAMERA_PIXELS_PER_WORLD_UNIT = 1;
export const CAMERA_PAN_STEP_PIXELS = 96;
export const CANVAS_MAX_PIXEL_RATIO = 4;
export const DEBUG_ENTITY_ROW_LIMIT = 100;

// The course is map geometry beneath entity layers; the terminal gate also has a heavier rim and
// a Finish label, so color is not its only distinction from an ordinary checkpoint.
export const RACE_CHECKPOINT_FILL = 'rgba(255, 255, 255, 0.7)';
export const RACE_CHECKPOINT_STROKE = '#1d4ed8';
export const RACE_FINISH_FILL = '#bbf7d0';
export const RACE_FINISH_STROKE = '#166534';
export const RACE_GATE_STROKE_WIDTH = 2;
export const RACE_GATE_LABEL_FONT = 'bold 12px system-ui';
