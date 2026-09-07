export const CONFIGURATION_RESPONSE_MAX_BYTES = 65_536;
export const CONFIGURATION_ENDPOINT_PATH = '/api/v1/config';

// Protocol v2 session transport. The route and the subprotocol are one pair: the server refuses a
// v1 token on this route, so neither value may be changed without the other.
export const SESSION_ENDPOINT_PATH = '/api/v2/session';
export const SESSION_WEBSOCKET_SUBPROTOCOL = 'blob-royale.session.v2';
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

export const CANVAS_MAX_WIDTH_PIXELS = 960;
export const CANVAS_MAX_HEIGHT_PIXELS = 640;
export const DEBUG_ENTITY_ROW_LIMIT = 100;
