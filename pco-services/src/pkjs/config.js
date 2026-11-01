// App settings. Nothing in this file is a secret.
module.exports = {
  // Client ID of the public OAuth app registered at https://api.planningcenteronline.com/oauth/applications.
  // The client ID is public. Never put a client secret here.
  CLIENT_ID: 'YOUR_PCO_CLIENT_ID',

  // HTTPS URL of login-page/index.html. Register this exact URL as the
  // redirect URI of the OAuth app.
  LOGIN_URL: 'https://YOUR-USERNAME.github.io/pco-pebble-login/',

  // Planning Center products this app reads. "services" covers plans,
  // live data and scheduling requests.
  SCOPE: 'services',

  // Sent with every API request. Planning Center asks for an app name plus contact.
  USER_AGENT: 'PCO Services for Pebble (https://github.com/YOUR-USERNAME/pco-pebble)',

  // true serves canned data with no network calls, for emulator screenshots.
  // Must be false in a release build.
  DEMO_MODE: false
};
