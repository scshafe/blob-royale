import '@testing-library/jest-dom/vitest';
import { cleanup } from '@testing-library/react';
import { afterEach } from 'vitest';

// Vitest runs without injected globals, so React Testing Library cannot register its own cleanup.
// Without this, one test's mounted tree stays in the document and the next test's role query finds
// two canvases and fails for a reason that has nothing to do with what it asserts.
afterEach(() => {
  cleanup();
});
