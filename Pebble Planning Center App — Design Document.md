# Pebble Planning Center App — Design Document

Oct 14, 2026 · @John

Build spec for a Pebble watchapp that shows Planning Center Services data, signs each user into their own account, and follows a live service in real time. Hand this document to Claude Code to start the build.

Checked against the Pebble developer docs ([developer.repebble.com](https://developer.repebble.com/), SDK 4.33.1) on Oct 17, 2026.

Updated Nov 1, 2026 after the first build. The code is in `pco-services/` and the login page is in `login-page/`. Changes from the first version of this spec are listed in "Changes after the first build" at the end.

## Goal and scope

The app runs on a Pebble watch. It reads data from Planning Center Services. Each user signs into a personal Planning Center account. The app follows a live service in real time.

The app reads plans and live state. It does not create or edit plans. The only write action is the optional accept or reject of a scheduling request.

## Requirements

**Must-have**

- View service orders. Show the plan order (items) for upcoming plans.
- Live service mode. Follow the live service clock and the current item in real time.
- Per-user login. Each user signs into a personal Planning Center account.
- No hard-coded credentials. Do not embed any secret or token in the app binary or source.

**Nice-to-have**

- Accept or reject scheduling requests from the watch. This is not a hard requirement. Defer it until the must-have features work.

## Authentication

**Decision: client-side only.** No backend server. No proxy. PebbleKit JS on the phone runs the OAuth flow and makes every API call. The token stays on the phone. A small static login page relays the one-time authorization code back to PebbleKit JS. The page holds no secret and never sees the token.

**Flow: OAuth 2 Authorization Code with PKCE.** Planning Center supports PKCE for public OAuth applications ([docs](https://api.planningcenteronline.com/docs/overview/authentication)). PKCE removes the need for a client secret. This is the correct flow for a client-side app.

Flow steps:

1. The user taps the app settings in the Pebble mobile app. This fires `showConfiguration` in PebbleKit JS.
2. PebbleKit JS creates a `code_verifier` and a random `state`. It stores both in `localStorage`.
3. Within 10 seconds, PebbleKit JS calls `Pebble.openURL()` with the login page URL. The URL carries the `code_challenge` and `state`.
4. The login page opens in a WebView inside the Pebble mobile app. It sends the user to the Planning Center authorize URL.
5. The user signs into a personal Planning Center account and approves access.
6. Planning Center redirects to the login page with `code` and `state`.
7. The login page navigates to `pebblejs://close#` plus the encoded `code` and `state`.
8. PebbleKit JS receives them in the `webviewclosed` event. It checks that `state` matches.
9. PebbleKit JS sends `code` and `code_verifier` to the token endpoint with `XMLHttpRequest`. No secret is sent.
10. PebbleKit JS stores the tokens in `localStorage`, deletes the verifier, and tells the watch that sign-in is done.

**Pebble rules for this flow** ([PebbleKit JS guide](https://developer.repebble.com/guides/communication/using-pebblekit-js/#configuration-pages))

- `Pebble.openURL()` works only inside the `showConfiguration` handler, within 10 seconds.
- The page opens in a WebView in the Pebble mobile app, not in the system browser.
- The page returns data only through `pebblejs://close#<data>`. Do not use `pebblejs://close?data=`.
- If the user leaves with the back button, `webviewclosed` does not fire. The watch keeps the sign-in prompt.
- Host the login page on HTTPS. A `data:` URL cannot receive an OAuth redirect.
- If the page URL has a `return_to` query parameter, use it in place of `pebblejs://close#`. The emulator sets it. Save it in `sessionStorage` before the redirect.
- Accept `return_to` only if it starts with `pebblejs://` or `http://localhost` / `http://127.0.0.1`. Any other value is ignored, so a crafted link cannot send the code to another site.
- Hard-code the authorize URL in the login page. Do not take it from a query parameter.

**Login page modes**

- Signed out: one button that sends the user to Planning Center.
- Signed in: the same page is the app's settings page. It has a "Vibrate when the live item changes" switch (off by default), a "Service types on your watch" checklist, "Sign in as someone else", and "Sign out". It returns `{action: "settings", vibe, hidden}` or `{action: "signout", vibe}` through `pebblejs://close#`.
- Service type filter: PebbleKit JS passes the service types seen on the last plans refresh (`types`, JSON `[[id, name], ...]`) and the hidden ids (`hidden`, comma-separated). The page shows a checkbox per type (checked = shown) and returns the unchecked ids. At least one type must stay checked. The phone stores hidden ids, not shown ids, so a new service type appears by default. Hidden types are skipped before their plans are requested. When the hidden set changes, PebbleKit JS sends `PlansChanged` and the watch reloads its plans. Sign-out clears the list and the filter. If the phone has not loaded plans yet, the page asks the user to open the watch app once first.
- After the redirect, the page returns `{action: "auth", code, state}` or `{action: "error", error}`.

**Endpoints**

- Authorize: `https://api.planningcenteronline.com/oauth/authorize`
- Token: `https://api.planningcenteronline.com/oauth/token`
- Redirect URI: the hosted login page URL, `https://rinvenom.github.io/pco-pebble/`. Register it in the Planning Center OAuth app.

**Token storage.** Store the access token and refresh token in PebbleKit JS `localStorage` on the phone side. PebbleKit JS makes every API call, so the tokens never cross Bluetooth to the watch. This storage is separate for each app UUID and persists across app launches. The access token expires. Use the refresh token to get a new access token. Run a refresh step when a request returns HTTP 401.

**Constraints**

- Warning: do not hard-code any client secret. Use PKCE only.
- Warning: do not embed a shared token. Each user gets a personal token.
- Warning: do not write tokens to `console.log()`. The Developer Connection log is not redacted.
- The OAuth client ID is public. It may ship in the app and the login page. The client ID is not a secret.
- PebbleKit JS `localStorage` survives an uninstall and reinstall. Sign out must delete the tokens with `localStorage.removeItem()`.
- Write with `localStorage.setItem()`, not property assignment. On Android, property writes can be lost if the phone app is killed.
- Reject the returned code if `state` does not match the stored value.
- The iOS PebbleKit JS runtime has no `crypto` object. SHA-256 for the `code_challenge` is bundled in `src/pkjs/sha256.js` (checked against the RFC 7636 test vector). Random values use `crypto.getRandomValues()` where it exists (Android) and fall back to `Math.random()` on iOS. The fallback is weaker. PKCE still protects the flow, because the verifier never leaves the phone.
- Sign out also revokes the refresh token at `https://api.planningcenteronline.com/oauth/revoke` (best effort).
- A refresh token that Planning Center rejects (HTTP 400 or 401) signs the user out on the phone and the watch.
- Planning Center asks every request to send a `User-Agent` with the app name and a contact. The iOS runtime sends it. Android's WebView does not allow this header and sends the browser's own value.

## Planning Center API reference

Product: Services. Base URL: `https://api.planningcenteronline.com/services/v2`. Data returns in JSON:API format.

**Data model**

- Service Type. A category of plan, such as a weekend service.
- Plan. One dated service instance.
- Item. One entry in a plan order, such as a song, announcement, or transition.
- Plan Times. The scheduled times for a plan.
- Live. The live controller state for a plan.

**Key endpoints**

| Purpose | Endpoint |
| --- | --- |
| List service types | `GET /service_types?per_page=100` |
| List plans for a type | `GET /service_types/{id}/plans?filter=after&after=<midnight today, ISO>&order=sort_date&per_page=4` |
| List items in a plan | `GET /service_types/{id}/plans/{id}/items?per_page=100&order=sequence` (follow `links.next`) |
| Live controller state | `GET /service_types/{id}/plans/{id}/live?include=current_item_time,next_item_time` |
| Current person | `GET /me` |
| Pending requests | `GET /people/{person_id}/schedules?filter=future` (keep `status` "U") |
| Status badges on plans | `GET /people/{person_id}/schedules?filter=with_declined,after&after=<midnight today>&per_page=100`; `relationships.plan.data.id` links to the plan, `status` is U, C or D. Declined schedules only come back with `with_declined`. Combining filters is not documented, so on a 4xx the app asks again with `filter=after` (no declined badges). Still to confirm against a real account. |
| Accept a request | `POST /people/{person_id}/schedules/{id}/accept` with body `{}` |
| Decline a request | `POST /people/{person_id}/schedules/{id}/decline` with `{"data":{"type":"ScheduleDecline","attributes":{"reason":""}}}` |

**Plan list filter.** Use `filter=after` with midnight today, not `filter=future`. `future` may drop a plan once its first service time starts, which would hide a running service from live mode.

**Live fields (confirmed in the docs).** The Live object has no clock fields itself. Its `current_item_time` and `next_item_time` relationships point to ItemTime records, returned in `included`. An ItemTime has `live_start_at`, `live_end_at`, `length` (seconds) and an `item` relationship. The phone computes the elapsed seconds from `live_start_at` and sends that number, so clock differences between phone and watch do not matter. Item titles come from the plan's items, which PebbleKit JS keeps for the session. If there is no `next_item_time`, the next item is the next non-header item in the order. Still to confirm during a real service: that `relationships` is filled in when the `include` is used. The code falls back to the started, unfinished ItemTime if it is not.

**Item fields.** `title`, `item_type` (`song`, `header`, `media`, `item`), `length`, `key_name`, `service_position` (`pre`, `during`, `post`), `sequence`.

**Scheduling (nice-to-have).** Built. The Schedule object has `service_type_name`, `short_dates`, `team_position_name` and `status`. The person ID comes from `GET /me` and is cached in `localStorage` (deleted at sign-out).

**Scopes.** `services` covers plans, live data and scheduling.

## App structure

**Platform**

- Pebble SDK 4.33.1. The watchapp is written in C. C runs on all seven platforms: aplite, basalt, chalk, diorite, emery, flint, and gabbro.
- Alloy (JavaScript on the watch) runs on emery and gabbro only. Use Alloy only if the app targets Pebble Time 2 and Pebble Round 2 alone.
- PebbleKit JS (`src/pkjs/index.js`) runs on the phone. It makes the network calls, stores the tokens, and opens the login page.
- Login page: a static HTML page on HTTPS hosting. It is on GitHub Pages at `https://rinvenom.github.io/pco-pebble/`, published from `login-page/` by `.github/workflows/pages.yml`. Do not use Clay for sign-in. Clay builds a local `data:` page, and an OAuth redirect cannot return to it.
- `package.json`: add `configurable` to `capabilities`. Declare every AppMessage key in `messageKeys`.

**Watchapp screens**

1. Sign-in prompt. A full-screen message that tells the user to open the app settings in the Pebble mobile app. Show it when no token is present. BACK leaves the app.
2. Main menu (`MenuLayer`). Lists upcoming plans by service type, up to 4 per type. The top row shows the cache age ("Updated 5m ago", "Offline · 2h ago"); SELECT on it refreshes. Long-press SELECT on a plan opens its live view. A last "More" section opens Requests. A small badge after a plan's date shows the user's schedule status: amber "?" = requested (not answered), green check = accepted, red X = declined, no badge = not scheduled. Black and white watches draw the same glyphs in black, so the shape carries the meaning, not only the color. Answering a request refreshes the list when the user returns to it.
3. Plan order view (`MenuLayer`). The first row opens the live view. Headers show as gray dividers, and the cursor skips over them when scrolling. Subtitles show key, length and pre/post-service. When the selected item's title or subtitle is too wide, it scrolls sideways: a 1 second pause, scroll to the end, a pause, then back to the start, three times, then it rests. Not on aplite, which has no memory to spare; long text there ends in "…".
4. Live view. The current item (28 pt, up to 2 lines), time left in the item counting down, "left of 4:30" or "over 4:30" in red, a progress bar, and the next item at the bottom. A `StatusBarLayer` shows the time of day. "Reconnecting..." replaces the label when the last good poll is over 20 seconds old.
5. Requests view (nice-to-have). Pending requests, then a detail screen with an `ActionBarLayer`: UP accepts, DOWN declines. The detail text is black and bold, with larger fonts on Time 2 and Round 2, and the date and position on separate lines. Rectangular screens show an "Up: accept / Down: decline" hint at the bottom; round screens rely on the action bar icons. A result dialog closes itself after 2 seconds.

**Touch (Time 2 and Round 2)**

- The app opts in to the system's touch navigation (`app_touch_navigation_enable(true)`, built only where `PBL_TOUCH` is defined). Menus scroll by swipe. A tap on a row selects it; a tap on the selected row opens it. Touches only act after the user wakes the watch or presses a button.
- Plan order: a tap on a section header that is more than one row away leaves the selection where it is. Buttons still skip headers.
- Request detail: the system maps action bar taps to buttons by thirds, but the round action bar draws both icons in the middle third. The detail screen also subscribes to `TouchService` while it is shown: a tap on the bar accepts in the top half and declines in the bottom half. Drags and touches without an interaction session are ignored, so the system back swipe still works.
- Not by touch: the long-press shortcut from a plan to its live view. The plan order's first row opens it instead.
- Emulator testing: `pebble` has no touch command, and the QEMU monitor's `mouse_move` is relative, so it does not reach the absolute touch device. Start QEMU with a QMP port (`PEBBLE_QEMU_PATH` pointing at a wrapper that adds `-qmp tcp:127.0.0.1:4445,server=on,wait=off`) and send `input-send-event` with `abs` x/y in 0–32767 plus a left button press.

**Data flow**

- The watch waits for a `JSReady` message from PebbleKit JS before it sends any request.
- The watch sends a request key over AppMessage. PebbleKit JS calls the API with `XMLHttpRequest` and returns flat key-value data.
- Warning: do not use `fetch()`. It is not defined in the iOS PebbleKit JS runtime.
- Warning: the iOS `XMLHttpRequest` has no `timeout` property. Use `setTimeout()` to cancel slow requests.
- Read the response from the request object, not from `event.target`. On iOS the event has only a `type` property.
- Send lists one item per message. Send the next item from the success callback of `Pebble.sendAppMessage()`.
- Send strings, integers, booleans, and byte arrays only. Nested objects stop the message. Undeclared keys are dropped with no error.
- Open AppMessage with fixed buffers: 512 bytes in, 256 bytes out. Each message carries one row, so this is enough. Register all callbacks before `app_message_open()`.
- Warning: AppMessage buffers use app heap memory. The maximum sizes take about 8 kB each, which aplite (24 kB for code and heap) cannot spare.
- Each request carries a `ReqId`. The watch ignores replies for an older request, and PebbleKit JS stops sending a list when a newer request arrives.
- The watch sends the list size it can hold (`Count`), and PebbleKit JS cuts strings to the watch buffer sizes. The watch also cuts at a UTF-8 character boundary.
- List sizes: 24 plans, 60 items, 12 requests. On aplite: 8, 16 and 4.
- Aplite has about 5 kB of heap left. Check `Free RAM available (heap)` for aplite after each build, and open the plan order, live view and a request detail in the aplite emulator. Avoid C library calls that pull in large code, such as `strstr()` (about 1 kB).
- Use a timeout and retry for each watch message. Retry after about 1 second, then back off.

**Live mode polling**

- Warning: on iOS, PebbleKit JS timers stop while the Pebble app is suspended in the background. Do not run the poll loop with `setInterval()` in PebbleKit JS.
- Run the poll timer on the watch with `app_timer_register()`. Each tick sends a refresh request to PebbleKit JS. Test this on iOS with the phone locked.
- Count the item clock on the watch between polls. Use the start time from the last response.
- Suggested interval: 5 seconds. The local clock stays smooth, and fewer messages save battery. Confirm against the Planning Center rate limits.
- Subscribe to `SECOND_UNIT` ticks only while the live view is open. Unsubscribe when the user leaves.
- Stop polling when the user leaves the live view.
- Handle HTTP 429 with a backoff. Planning Center enforces rate limits per application and per token.

## Pebble best practices

These come from the Pebble developer guides: [Best Practices](https://developer.repebble.com/guides/best-practices/), [Advanced Communication](https://developer.repebble.com/guides/communication/advanced-communication/), [Persistent Storage](https://developer.repebble.com/guides/events-and-services/persistent-storage/), and [Recommended Guidelines](https://developer.repebble.com/guides/design-and-interaction/recommended/).

**Startup and connection**

- Wait for the `JSReady` message before the first request.
- Call `connection_service_peek_pebble_app_connection()` before a request. Show a full-screen alert if the phone is not connected.
- Show a separate alert when the phone has no internet connection.

**Caching and state**

- Cache the last loaded plan list in watch persistent storage. Show it with its age until new data arrives.
- Keep each stored value at 256 bytes or less (`PERSIST_DATA_MAX_LENGTH`).
- Read the total limit with `persist_get_max_size()`. It is 4 kB on older watches and 1 MB on Pebble 2 Duo, Time 2, and Round 2.
- Store a storage version number. Migrate or delete old data when the format changes.
- Read and write storage at launch or exit, not during scrolling.
- Remember the last service type the user opened. Open it first on the next launch. (The watch sends it as `LastServiceType`; PebbleKit JS lists that type first.)
- Each cached plan row is 137 bytes (storage version 2 added the schedule status). On watches with 4 kB of storage the watch caches the first 16 plans, and stops if a write fails. Sign-out deletes the cached plans on the watch.
- PebbleKit JS starts a fresh context on each launch. Keep state that must survive in `localStorage`.

**Screens on every watch**

- Do not hard-code screen sizes. Use `layer_get_unobstructed_bounds()` on the window root layer.
- Use `PBL_ROUND`, `PBL_COLOR`, and the `PBL_IF_*_ELSE()` macros for shape and color. Prefer feature defines over `PBL_PLATFORM_*`.
- Screen sizes: 144 × 168 (aplite, basalt, diorite, flint), 180 × 180 round (chalk), 200 × 228 (emery), 260 × 260 round (gabbro).
- Use font size 28 for the current item. Use 18 or larger for other text.
- Respect the Text Size setting (`preferred_content_size()`). The live view and request detail use their larger fonts only on tall screens (Time 2, Round 2) at the default or a larger Text Size; a smaller Text Size there gives the smaller fonts. Smaller screens already use the largest fonts that fit. Menus use the system cell drawing.
- On short screens the live view drops the progress bar before it cuts the current item's name; the label under the clock still says how much time is left.

**Battery**

- Avoid long or repeated animations.
- Do not force the backlight on with `light_enable()`.
- Keep vibrations short and few. If the live view vibrates on item change, make that a setting that is off by default.
- Leave the Bluetooth sniff interval at `SNIFF_INTERVAL_NORMAL`.

**Optional: timeline pins**

- PebbleKit JS can add local pins with `Pebble.insertTimelinePin()`. A pin for each plan the user serves on would show on the watch timeline.
- The timeline web API is no longer available. Local pins need no server and no API key.

## Claude Code setup

From the [Building with AI Agents](https://developer.repebble.com/guides/ai-agents/) guide.

- Create the project with `pebble new-project --ai <name>`. This writes a `CLAUDE.md` with the project layout, build commands, and doc links.
- Add this line to `CLAUDE.md`: `Pebble SDK docs: https://developer.repebble.com/llms.txt. Fetch the .md version of any page before using an API.`
- State the SDK version (4.33.1) and the `targetPlatforms` list in `CLAUDE.md`.
- Tell Claude Code to look up C and PebbleKit JS symbols in `https://developer.repebble.com/api-index.json`.
- Give Claude Code this design document as the spec.

Emulator commands Claude Code can run:

```
pebble build
pebble install --emulator emery
pebble screenshot --no-open --emulator emery screenshot.png
pebble emu-button click select --emulator emery
pebble logs --emulator emery
```

- Add `--vnc` to each emulator command in a headless environment.
- If the emulator hangs, run `pebble kill`, then `pebble wipe`, then install again.
- Optional: the [Pebble agent skill](https://github.com/coredevices/pebble-watchface-agent-skill) adds a build, screenshot, and compare loop for Claude Code.

**Testing the sign-in flow**

- Warning: the `pebble` tool runs its own PebbleKit JS, not the iOS and Android runtimes. Test sign-in and live polling in the Pebble mobile app.
- Use a real watch with [Developer Connection](https://developer.repebble.com/guides/tools-and-resources/developer-connection/). Or connect the Pebble mobile app to a PebbleOS emulator built from source.
- Test on both iOS and Android. The two runtimes differ.

## Build order and open items

**Build order for Claude Code**

Status as of Nov 1, 2026: ✅ done, ⏳ waiting on someone, 🔲 not started.

1. ✅ Scaffold with `pebble new-project --ai`. Set `targetPlatforms` and add `configurable`.
2. ⏳ Register a Planning Center OAuth app. Set the redirect URI to the hosted login page. Needs an Organization Administrator.
3. ✅ Build the static login page. It relays `code` and `state` to `pebblejs://close#`. Hosted on GitHub Pages.
4. ✅ Build PKCE sign-in in PebbleKit JS. Not yet confirmed against Planning Center (needs steps 2 and 3).
5. ✅ Build the token refresh on HTTP 401, and build sign-out.
6. ✅ Build the `JSReady` handshake and an AppMessage helper with timeout and retry.
7. ✅ Build the sign-in prompt and the main menu.
8. ✅ Build the plan order view.
9. ✅ Build the live view with watch-driven polling and a local clock.
10. 🔲 Test sign-in and live mode on iOS and Android against a real plan.
11. ✅ Add the scheduling accept and decline view.

All screens were checked in the emulator on all seven platforms with demo data (`DEMO_MODE` in `src/pkjs/config.js`). Screenshots are in `pco-services/screenshots/`.

**Open items**

Answered during the build:

- ~~The exact field names on the live endpoint.~~ See "Live fields" above. One detail is left to check during a real service.
- ~~The exact scheduling confirmation endpoint and payload.~~ See the endpoint table.
- ~~The Pebble platform targets.~~ C, all seven platforms.
- ~~The OAuth scopes needed for Services and for scheduling.~~ `services` only.
- ~~Where to host the login page.~~ GitHub Pages: `https://rinvenom.github.io/pco-pebble/`. The redirect URI must match `LOGIN_URL` in `config.js` exactly, including the trailing slash.

Still open:

- The OAuth client ID, and who registers the app.
- The app name. The build uses "PCO Services". A name that does not look like an official Planning Center app may be safer for the App Store.
- Whether the token endpoint accepts requests from Android PebbleKit JS. It runs in a hidden WebView, where browser CORS rules can apply. Planning Center documents a browser-only PKCE demo, so this is expected to work.
- Whether watch-driven polling keeps PebbleKit JS awake on iOS with the phone locked.
- The "No phone" alert on a real watch. The emulator reconnects its phone link at once, so it could not be tested there.
- Optional timeline pins. Not built.

## Changes after the first build

| Area | First version | Now | Why |
| --- | --- | --- | --- |
| Plan list | `filter=future` | `filter=after` from midnight today | Keeps a running service in the list for live mode. |
| Live data | Fields to confirm | Live + `include=current_item_time,next_item_time`; ItemTime gives start and length | Live has no clock fields of its own. |
| AppMessage buffers | Maximum sizes | 512 in / 256 out | Maximum sizes do not fit next to menus on aplite. |
| PKCE randomness | Not specified | `crypto.getRandomValues()`, else `Math.random()` on iOS | iOS PebbleKit JS has no `crypto`. |
| SHA-256 | Not specified | Bundled in `sha256.js` | No `crypto.subtle` on iOS. |
| `return_to` | Use as given | Only `pebblejs://` or localhost | Stops a crafted link from sending the code elsewhere. |
| Settings and sign-out | Not placed | Login page, signed-in mode | One page for sign-in, sign-out and the vibrate setting. |
| Sign-out | Delete tokens | Delete tokens and revoke the refresh token | Ends the session at Planning Center too. |
| User-Agent | Not mentioned | Sent where the runtime allows | Planning Center may reject requests without one. |
| Live shortcut | Not in spec | Long-press a plan opens live view | One step fewer during a service. |
| Aplite lists | Not specified | 8 plans, 16 items, 4 requests | Memory. |
| Service type filter | Not in spec | Checklist on the settings page; hidden types are not fetched | Users asked to choose which service types the watch shows. |
| Plan order headers | Selectable | Skipped by the cursor | Headers are dividers, not choices. |
| Request detail text | 18 pt, gray hint | Bold black; 28/24 pt on Time 2 and Round 2 | Hard to read on a Time 2. |
| Long plan item text | Cut with "…" | Scrolls sideways when selected (not on aplite) | Long titles could not be read. |
| Touch | Not in spec | System touch navigation on Time 2 and Round 2, plus half-bar taps on the request detail | Use the touchscreen; round action bar icons sit between the system's tap zones. |
| Schedule status badges | Not in spec | Requested/accepted/declined badge on each main menu plan, from `/people/{id}/schedules` (`filter=with_declined,after`, falling back to `after`) | See at a glance which plans need an answer. |
| Plan cache on 4 kB watches | All plans | First 16, write results checked | 24 rows plus overhead could exceed the 4 kB limit. |
| Text Size | Not followed | Large fonts only on tall screens and not at a smaller Text Size | Content Size guide; the Time Round's big clock also cut the item name to one line. |
