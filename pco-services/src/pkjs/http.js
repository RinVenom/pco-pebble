// XMLHttpRequest wrapper that works in both PebbleKit JS runtimes.
// - iOS XHR has no `timeout` property, so a setTimeout() aborts slow requests.
// - iOS events carry only `type`, so results are read from the request object.
// - fetch() is not used: it does not exist on iOS.

var config = require('./config');

var TIMEOUT_MS = 15000;

// cb(result) where result = { status, json, text, retryAfter }.
// status 0 means no response (offline, DNS failure, timeout or CORS block).
function request(method, url, headers, body, cb) {
  var xhr = new XMLHttpRequest();
  var finished = false;

  function finish(status) {
    if (finished) return;
    finished = true;
    clearTimeout(timer);
    var text = '';
    try { text = xhr.responseText || ''; } catch (e) { text = ''; }
    var json = null;
    if (text) {
      try { json = JSON.parse(text); } catch (e) { json = null; }
    }
    var retryAfter = 0;
    if (status === 429) {
      try { retryAfter = parseInt(xhr.getResponseHeader('Retry-After'), 10) || 0; } catch (e) { retryAfter = 0; }
      if (!retryAfter) retryAfter = 20;
    }
    cb({ status: status, json: json, text: text, retryAfter: retryAfter });
  }

  var timer = setTimeout(function () {
    try { xhr.abort(); } catch (e) { /* ignore */ }
    finish(0);
  }, TIMEOUT_MS);

  xhr.onload = function () { finish(xhr.status); };
  xhr.onerror = function () { finish(0); };
  xhr.onabort = function () { finish(0); };

  try {
    xhr.open(method, url, true);
    xhr.setRequestHeader('Accept', 'application/json');
    // Planning Center asks every client to identify itself. Browsers (the
    // Android WebView) ignore this header, which Planning Center accepts for
    // browser clients; the iOS runtime sends it.
    try { xhr.setRequestHeader('User-Agent', config.USER_AGENT); } catch (e) { /* forbidden header */ }
    if (headers) {
      for (var name in headers) {
        if (headers.hasOwnProperty(name)) xhr.setRequestHeader(name, headers[name]);
      }
    }
    xhr.send(body || null);
  } catch (e) {
    finish(0);
  }
}

function formEncode(obj) {
  var parts = [];
  for (var k in obj) {
    if (obj.hasOwnProperty(k) && obj[k] !== undefined && obj[k] !== null) {
      parts.push(encodeURIComponent(k) + '=' + encodeURIComponent(obj[k]));
    }
  }
  return parts.join('&');
}

module.exports = {
  request: request,
  formEncode: formEncode
};
