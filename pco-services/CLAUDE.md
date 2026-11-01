@AGENTS.md

## This project

Pebble watchapp for Planning Center Services. The spec is
`../Pebble Planning Center App — Design Document.md`. Read it before changing behavior.

- Pebble SDK 4.33.1. C watchapp. `targetPlatforms`: aplite, basalt, chalk, diorite, emery, flint, gabbro.
- Pebble SDK docs: https://developer.repebble.com/llms.txt. Fetch the .md version of any page before using an API.
- Look up C and PebbleKit JS symbols in https://developer.repebble.com/api-index.json.
- Planning Center API docs: https://developer.planning.center/docs/ (JavaScript site; read it in a browser).

Layout:
- `src/c/` watch app. `main.c` (startup, routing), `comm.c` (AppMessage with timeout/retry),
  `data.c` (stores + persistent cache), `ui_*.c` (one file per screen).
- `src/pkjs/` phone side. `index.js` (messaging), `auth.js` (OAuth PKCE + refresh), `pco.js` (API calls),
  `sha256.js`, `config.js` (client ID, login page URL, demo mode).
- `../login-page/index.html` static HTTPS page that relays the OAuth code to `pebblejs://close#`.

Rules:
- Never log tokens. Never add a client secret. Use `localStorage.setItem/removeItem`.
- No `fetch()` in pkjs (not on iOS). Read XHR results from the request object.
- `config.js` `DEMO_MODE` must be `false` in a release build. Demo mode serves canned data for emulator screenshots.
