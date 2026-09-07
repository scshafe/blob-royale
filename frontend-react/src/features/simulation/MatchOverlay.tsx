import type { MatchOverlayDescription } from './sessionSelectors';

export interface MatchOverlayProps {
  readonly description: MatchOverlayDescription | null;
}

/**
 * Announces the match state over the arena. It is a live region rather than a status role because
 * the connection status owns the page's one status role, and two status roles read as one voice.
 */
export function MatchOverlay({ description }: MatchOverlayProps) {
  return (
    <div aria-live="polite" className="MatchOverlay">
      {description === null ? null : (
        <div className="MatchOverlayCard">
          <p className="MatchOverlayTitle">{description.title}</p>
          <p className="MatchOverlayDetail">{description.detail}</p>
        </div>
      )}
    </div>
  );
}
