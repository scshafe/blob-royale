import {
  describeLobbyListing,
  describeRoomRefusal,
} from './lobbyDirectorySelectors';
import type { LobbyDirectoryState } from './useLobbyDirectory';
import type { RoomNotice } from './useRoomNavigation';

export interface LobbyDirectoryViewProps {
  readonly directory: LobbyDirectoryState;
  /** What the player was just told about the room they were sent back from, if anything. */
  readonly notice: RoomNotice | null;
  readonly onJoin: (lobbyId: number) => void;
}

const directoryStatusLabels = Object.freeze({
  failed: 'The room list could not be read. Trying again every second.',
  idle: 'The room list is paused.',
  loading: 'Reading the rooms…',
  ready: 'Rooms refresh once a second while this list is open.',
});

/**
 * The list view: every room the server runs, as the directory last listed it, and one Join per
 * room. It is presentational -- every string comes from the selectors, every decision about
 * whether a room is worth joining is the directory's own columns read through one rule -- and it
 * owns no socket and no timer.
 */
export function LobbyDirectoryView({
  directory,
  notice,
  onJoin,
}: LobbyDirectoryViewProps) {
  return (
    <section
      aria-labelledby="lobby-directory-heading"
      className="LobbyDirectory"
    >
      <h2 id="lobby-directory-heading">Rooms</h2>
      <p aria-live="polite" role="status">
        {directoryStatusLabels[directory.status]}
      </p>
      {directory.error === null ? null : (
        <p className="ConnectionError">
          {directory.error.code}: {directory.error.message}
        </p>
      )}
      {notice === null ? null : (
        <p className="RoomNotice" role="alert">
          {notice.message}
        </p>
      )}
      {directory.listings.length === 0 ? null : (
        <ul className="LobbyList">
          {directory.listings.map((listing) => {
            const description = describeLobbyListing(listing);
            return (
              <li className="LobbyCard" key={listing.lobby_id}>
                <h3>{description.title}</h3>
                <dl className="LobbyFacts">
                  <div>
                    <dt>Mode</dt>
                    <dd>{description.modeAndMap}</dd>
                  </div>
                  <div>
                    <dt>Phase</dt>
                    <dd>{description.phaseLabel}</dd>
                  </div>
                  <div>
                    <dt>Seats</dt>
                    <dd>{description.seatsLabel}</dd>
                  </div>
                  <div>
                    <dt>In the room</dt>
                    <dd>{description.occupancyLabel}</dd>
                  </div>
                </dl>
                <button
                  disabled={!description.joinable}
                  onClick={() => {
                    onJoin(listing.lobby_id);
                  }}
                  type="button"
                >
                  Join {description.title}
                </button>
                {description.refusal === null ? null : (
                  <p className="LobbyRefusal">
                    {describeRoomRefusal(description.refusal, listing.lobby_id)}
                  </p>
                )}
              </li>
            );
          })}
        </ul>
      )}
    </section>
  );
}
