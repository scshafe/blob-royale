import type { PropsWithChildren } from 'react';

export interface SimulationShellProps {
  /** The room the page is in, or `null` on the directory. */
  readonly lobbyId: number | null;
  readonly onLeave: () => void;
}

/**
 * The persistent frame around every view: who this app is, where in it the player is, and the way
 * back up. The header is the same on the directory and in a room -- only the breadcrumb's last
 * segment and the Leave control change -- so switching rooms never reads as switching apps. "Up"
 * is explicit here because the browser's back button is a different thing: back returns to
 * wherever the player came from, and up always returns to the rooms.
 */
export function SimulationShell({
  children,
  lobbyId,
  onLeave,
}: PropsWithChildren<SimulationShellProps>) {
  return (
    <>
      <header className="AppHeader">
        <h1 className="AppTitle">Blob Royale</h1>
        <nav aria-label="Rooms" className="AppBreadcrumbs">
          <ol>
            <li>
              {lobbyId === null ? (
                <span aria-current="page">Rooms</span>
              ) : (
                <a
                  href={window.location.pathname}
                  onClick={(event) => {
                    event.preventDefault();
                    onLeave();
                  }}
                >
                  Rooms
                </a>
              )}
            </li>
            {lobbyId === null ? null : (
              <li aria-current="page">Room {lobbyId}</li>
            )}
          </ol>
        </nav>
        {lobbyId === null ? null : (
          <button className="LeaveButton" onClick={onLeave} type="button">
            Leave room
          </button>
        )}
      </header>
      <main className="AppContent">{children}</main>
    </>
  );
}
