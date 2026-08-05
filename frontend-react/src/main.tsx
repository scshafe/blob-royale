import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';

import App from './App';
import './index.css';

const rootElement = document.getElementById('root');

if (rootElement === null) {
  throw new Error('WEB.ROOT_ELEMENT_MISSING: expected #root in index.html');
}

createRoot(rootElement).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
