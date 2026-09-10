export const CONFIGURATION_RESPONSE_MAX_BYTES = 65_536;
export const CONFIGURATION_ENDPOINT_PATH = '/api/v1/config';

// Protocol v2 rooms. The directory is the one HTTP read v2 offers, and a client polls it only
// while it is choosing a room; a room's session target carries the one parametric segment v2 has,
// `[1-9][0-9]{0,2}`, and `/api/v2/lobbies/1/session` names the same room `/api/v2/session` does.
// The route and the subprotocol are one pair: the server refuses a v1 token on a session route, so
// neither value may be changed without the other.
export const LOBBY_DIRECTORY_ENDPOINT_PATH = '/api/v2/lobbies';
export const ROOM_SESSION_ENDPOINT_PATH_PREFIX = '/api/v2/lobbies/';
export const ROOM_SESSION_ENDPOINT_PATH_SUFFIX = '/session';
export const LOBBY_ID_PATTERN = /^[1-9][0-9]{0,2}$/;
export const SESSION_WEBSOCKET_SUBPROTOCOL = 'blob-royale.session.v2';
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
export const RACE_COURSE_FILL = '#dbeafe';
export const RACE_CHECKPOINT_FILL = 'rgba(255, 255, 255, 0.7)';
export const RACE_CHECKPOINT_STROKE = '#1d4ed8';
export const RACE_FINISH_FILL = '#bbf7d0';
export const RACE_FINISH_STROKE = '#166534';
export const RACE_GATE_STROKE_WIDTH = 2;
export const RACE_GATE_LABEL_FONT = 'bold 12px system-ui';
