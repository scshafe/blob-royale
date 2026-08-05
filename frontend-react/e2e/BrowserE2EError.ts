/** Carries one machine-readable browser-E2E failure with bounded context. */
export class BrowserE2EError extends Error {
  readonly code: string;
  readonly context: Readonly<Record<string, number | string | null>>;

  constructor(
    code: string,
    message: string,
    context: Readonly<Record<string, number | string | null>> = {},
    cause?: unknown,
  ) {
    super(
      `${code}: ${message}; context=${JSON.stringify(context)}`,
      cause === undefined ? undefined : { cause },
    );
    this.name = 'BrowserE2EError';
    this.code = code;
    this.context = context;
  }
}
