// Planning Center sign-in: OAuth 2 Authorization Code with PKCE.
// No client secret exists anywhere in this app. Tokens stay in this
// phone's PebbleKit JS localStorage and are never sent to the watch.
// Never pass a token, code or verifier to console.log().

var config = require('./config');
var http = require('./http');
var sha = require('./sha256');

var TOKEN_URL = 'https://api.planningcenteronline.com/oauth/token';
var REVOKE_URL = 'https://api.planningcenteronline.com/oauth/revoke';

var K_ACCESS = 'pco.access';
var K_REFRESH = 'pco.refresh';
var K_EXPIRES = 'pco.expiresAt';
var K_VERIFIER = 'pco.verifier';
var K_STATE = 'pco.state';
var K_PERSON = 'pco.personId';

// Error codes shared with the watch (see comm.h).
var ERR = {
  AUTH: 1,        // not signed in, or the refresh token was rejected
  NETWORK: 2,     // no internet / no response
  RATE_LIMIT: 3,  // HTTP 429
  SERVER: 4,      // any other failure
  NOT_FOUND: 5
};

function now() { return Math.floor(Date.now() / 1000); }

// Random string from the URL-safe alphabet. Uses crypto.getRandomValues
// where it exists (Android WebView). The iOS runtime has no crypto object,
// so it falls back to Math.random mixed with the clock.
function randomString(len) {
  var alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
  var out = '';
  var bytes = null;
  try {
    if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
      bytes = new Uint8Array(len);
      crypto.getRandomValues(bytes);
    }
  } catch (e) { bytes = null; }
  var seed = Date.now();
  for (var i = 0; i < len; i++) {
    var b = bytes ? bytes[i] : (Math.floor(Math.random() * 256) ^ ((seed >>> (i % 24)) & 0xff));
    out += alphabet.charAt(b & 63);
  }
  return out;
}

function isConfigured() {
  return config.CLIENT_ID.indexOf('YOUR_') !== 0 && config.LOGIN_URL.indexOf('YOUR-') === -1;
}

function isSignedIn() {
  if (config.DEMO_MODE) return true;
  return !!localStorage.getItem(K_REFRESH) || !!localStorage.getItem(K_ACCESS);
}

// Builds the login page URL for Pebble.openURL(). Creates a fresh verifier
// and state each time and stores them until the page returns.
// mode: 'signin' or 'manage' (signed in: shows sign out and settings).
// extra: more query parameters for the settings page.
function buildLoginUrl(mode, vibe, extra) {
  var verifier = randomString(64);
  var state = randomString(32);
  localStorage.setItem(K_VERIFIER, verifier);
  localStorage.setItem(K_STATE, state);
  var challenge = sha.base64url(sha.sha256Bytes(verifier));
  var params = {
    mode: mode,
    client_id: config.CLIENT_ID,
    scope: config.SCOPE,
    code_challenge: challenge,
    state: state,
    vibe: vibe ? '1' : '0'
  };
  for (var k in (extra || {})) params[k] = extra[k];
  return config.LOGIN_URL + '?' + http.formEncode(params);
}

function storeTokens(json) {
  localStorage.setItem(K_ACCESS, json.access_token);
  if (json.refresh_token) localStorage.setItem(K_REFRESH, json.refresh_token);
  var ttl = parseInt(json.expires_in, 10) || 7200;
  localStorage.setItem(K_EXPIRES, String(now() + ttl));
}

function clearTokens() {
  localStorage.removeItem(K_ACCESS);
  localStorage.removeItem(K_REFRESH);
  localStorage.removeItem(K_EXPIRES);
  localStorage.removeItem(K_VERIFIER);
  localStorage.removeItem(K_STATE);
  localStorage.removeItem(K_PERSON);
}

// Exchanges the code from the login page. cb(errCode or 0).
function exchangeCode(code, returnedState, cb) {
  var expectedState = localStorage.getItem(K_STATE);
  var verifier = localStorage.getItem(K_VERIFIER);
  // The state check rejects codes that this phone did not ask for.
  if (!expectedState || !verifier || returnedState !== expectedState) {
    console.log('Sign-in rejected: state mismatch');
    localStorage.removeItem(K_STATE);
    localStorage.removeItem(K_VERIFIER);
    cb(ERR.AUTH);
    return;
  }
  var body = http.formEncode({
    grant_type: 'authorization_code',
    code: code,
    code_verifier: verifier,
    client_id: config.CLIENT_ID,
    redirect_uri: config.LOGIN_URL
  });
  http.request('POST', TOKEN_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, body,
    function (res) {
      localStorage.removeItem(K_VERIFIER);
      localStorage.removeItem(K_STATE);
      if (res.status === 200 && res.json && res.json.access_token) {
        storeTokens(res.json);
        console.log('Signed in');
        cb(0);
      } else {
        console.log('Token exchange failed, HTTP ' + res.status);
        cb(res.status === 0 ? ERR.NETWORK : ERR.AUTH);
      }
    });
}

// Refresh with a lock so parallel API calls share one refresh request.
var refreshWaiters = null;
function refresh(cb) {
  if (refreshWaiters) { refreshWaiters.push(cb); return; }
  var refreshToken = localStorage.getItem(K_REFRESH);
  if (!refreshToken) { cb(ERR.AUTH); return; }
  refreshWaiters = [cb];
  var body = http.formEncode({
    grant_type: 'refresh_token',
    refresh_token: refreshToken,
    client_id: config.CLIENT_ID
  });
  http.request('POST', TOKEN_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, body,
    function (res) {
      var err = 0;
      if (res.status === 200 && res.json && res.json.access_token) {
        storeTokens(res.json);
      } else if (res.status === 400 || res.status === 401) {
        // Refresh token expired (90 days unused) or revoked: sign in again.
        console.log('Refresh rejected, HTTP ' + res.status);
        clearTokens();
        err = ERR.AUTH;
      } else {
        err = res.status === 0 ? ERR.NETWORK : ERR.SERVER;
      }
      var waiters = refreshWaiters;
      refreshWaiters = null;
      for (var i = 0; i < waiters.length; i++) waiters[i](err);
    });
}

// cb(errCode, accessToken). Refreshes first if the token is about to expire.
function getToken(cb) {
  var access = localStorage.getItem(K_ACCESS);
  var expiresAt = parseInt(localStorage.getItem(K_EXPIRES), 10) || 0;
  if (access && expiresAt - 60 > now()) { cb(0, access); return; }
  refresh(function (err) {
    if (err) { cb(err); return; }
    cb(0, localStorage.getItem(K_ACCESS));
  });
}

// Revokes the refresh token (best effort) and deletes everything stored.
// localStorage survives an uninstall, so this is the only way tokens go away.
function signOut(cb) {
  var refreshToken = localStorage.getItem(K_REFRESH);
  clearTokens();
  if (!refreshToken || config.DEMO_MODE) { cb(); return; }
  var body = http.formEncode({
    token: refreshToken,
    token_type_hint: 'refresh_token',
    client_id: config.CLIENT_ID
  });
  http.request('POST', REVOKE_URL, { 'Content-Type': 'application/x-www-form-urlencoded' }, body,
    function () { cb(); });
}

module.exports = {
  ERR: ERR,
  isConfigured: isConfigured,
  isSignedIn: isSignedIn,
  buildLoginUrl: buildLoginUrl,
  exchangeCode: exchangeCode,
  getToken: getToken,
  refresh: refresh,
  signOut: signOut,
  K_PERSON: K_PERSON
};
