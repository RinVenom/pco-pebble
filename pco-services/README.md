# PCO Services for Pebble

A Pebble watchapp for Planning Center Services: upcoming plans, plan orders,
a live view that follows the service in real time, and accepting or
declining scheduling requests. Each person signs in with their own
Planning Center account. There is no server and no client secret.

Spec: `../Pebble Planning Center App — Design Document.md`.

## Before the first real build

1. **Register a public OAuth app.** An Organization Administrator signs in at
   https://api.planningcenteronline.com/oauth/applications and creates one
   OAuth application. PKCE is required for public apps.
   - Redirect URI: `https://rinvenom.github.io/pco-pebble/` (exactly, with
     the trailing slash).
   - Scope used: `services`.
2. **Login page.** Already hosted on GitHub Pages at
   https://rinvenom.github.io/pco-pebble/. The workflow
   `../.github/workflows/pages.yml` republishes `../login-page/` on every push
   to `main` that changes it. The page holds no secret.
3. **Fill in `CLIENT_ID` in `src/pkjs/config.js`.** `LOGIN_URL` and
   `USER_AGENT` are already set. `LOGIN_URL` must match the redirect URI
   character for character.
4. Keep `DEMO_MODE: false`.

## Build and run

```bash
pebble build
pebble install --emulator emery
```

On a watch: enable Developer Connection in the Pebble app, then
`pebble install --phone <ip>` or `pebble install --cloudpebble`.
Open the app's **Settings** in the Pebble app on the phone to sign in.

## Demo mode

Set `DEMO_MODE: true` in `src/pkjs/config.js` to serve canned plans, items, a
running live service and two requests, with no network. This is how the
screenshots in `screenshots/` were made. Turn it off before release.

## How it fits together

| Part | Where | Does |
| --- | --- | --- |
| Watch app (C) | `src/c/` | Menus, live view, polling timer, cache in persistent storage |
| PebbleKit JS | `src/pkjs/` | OAuth PKCE, token refresh on 401, API calls, AppMessage |
| Login page | `../login-page/` | Sends the user to Planning Center, relays `code` + `state` to `pebblejs://close#` |

Watch ⇄ phone protocol: the watch sends `Request` + `ReqId` (+ ids). The phone
answers with one message per list row (`Index`, `Count`, fields) or an
`Error` code. Codes are listed in `src/c/comm.h` and `src/pkjs/auth.js`.

## Buttons

- Main menu: SELECT opens a plan; **long-press SELECT** opens its live view.
  The top row shows the cache age; SELECT on it refreshes.
- Plan order: first row opens the live view. Section headers are skipped,
  and a selected item whose text is too long scrolls sideways (not on aplite).
- Requests: UP accepts, DOWN declines.

## Settings page

Signed in, the app's **Settings** in the Pebble app opens the same hosted
page as a settings page: vibrate on item change, which service types the
watch shows, switch account and sign out. The service type list is the one
seen on the last plans refresh, so open the watch app once after signing in.
Changing the service types makes a running watch app reload its plans.

## Still to test on real hardware

The `pebble` tool runs its own PebbleKit JS, not the iOS and Android ones:

- Sign-in end to end on iOS and Android (Android's token request may be
  subject to CORS in its WebView; Planning Center documents a browser-only
  PKCE demo, so it is expected to work).
- Live polling on iOS with the phone locked.
- The "No phone" alert: the emulator reconnects its phone link at once.
- Real field values from the Live endpoint during a running service.
