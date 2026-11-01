// PebbleKit JS entry point. Runs on the phone.
// The watch sends one request at a time (Request + ReqId). This file calls
// Planning Center and answers with flat messages, one list row per
// message, each sent from the success callback of the one before.

var config = require('./config');
var auth = require('./auth');
var source = config.DEMO_MODE ? require('./demo') : require('./pco');

// Request types shared with the watch (see comm.h).
var REQ = { PLANS: 1, ITEMS: 2, LIVE: 3, REQUESTS: 4, RESPOND: 5 };
var ERR = auth.ERR;

var K_VIBE = 'pco.vibeOnChange';

// String lengths match the watch buffers (data.h). Cut here to save airtime;
// the watch also cuts safely at a UTF-8 boundary.
var MAX = { SECTION: 23, TITLE: 39, SUBTITLE: 39, ID: 15 };

var currentReqId = -1;

function cut(s, n) {
  s = s === undefined || s === null ? '' : String(s);
  return s.length > n ? s.substring(0, n) : s;
}

// Sends messages in order. Stops if the watch has moved on to a newer
// request. Retries each message up to 3 times, 1 s then 2 s apart.
function sendSequence(msgs, reqId, done) {
  var i = 0, attempt = 0;
  function next() {
    if (reqId >= 0 && reqId !== currentReqId) return; // superseded
    if (i >= msgs.length) { if (done) done(); return; }
    var msg = msgs[i];
    if (reqId >= 0) msg.ReqId = reqId;
    Pebble.sendAppMessage(msg, function () {
      i++; attempt = 0; next();
    }, function () {
      attempt++;
      if (attempt > 3) { console.log('Giving up on message ' + i); return; }
      setTimeout(next, 1000 * attempt);
    });
  }
  next();
}

function sendError(reqId, code, retryAfter) {
  var msg = { Error: code };
  if (retryAfter) msg.RetryAfter = retryAfter;
  sendSequence([msg], reqId);
  if (code === ERR.AUTH) sendAuthState();
}

function sendAuthState() {
  sendSequence([{ AuthState: auth.isSignedIn() ? 1 : 0 }], -1);
}

function sendVibeSetting() {
  sendSequence([{ VibeOnChange: localStorage.getItem(K_VIBE) === '1' ? 1 : 0 }], -1);
}

function listMessages(rows, toMsg) {
  if (!rows.length) return [{ Index: 0, Count: 0 }];
  return rows.map(function (r, idx) {
    var m = toMsg(r);
    m.Index = idx;
    m.Count = rows.length;
    return m;
  });
}

function handleRequest(p) {
  var reqId = p.ReqId || 0;
  currentReqId = reqId;
  var type = p.Request;
  var cap = p.Count || 20;

  if (!auth.isSignedIn()) { sendError(reqId, ERR.AUTH); return; }

  if (type === REQ.PLANS) {
    source.getPlans(p.LastServiceType || '', cap, function (err, rows, ra) {
      if (err) { sendError(reqId, err, ra); return; }
      sendSequence(listMessages(rows, function (r) {
        return {
          Section: cut(r.section, MAX.SECTION),
          ServiceTypeId: cut(r.serviceTypeId, MAX.ID),
          PlanId: cut(r.planId, MAX.ID),
          Title: cut(r.title, MAX.TITLE),
          Subtitle: cut(r.subtitle, MAX.SUBTITLE)
        };
      }), reqId);
    });
  } else if (type === REQ.ITEMS) {
    source.getItems(p.ServiceTypeId, p.PlanId, cap, function (err, items, ra) {
      if (err) { sendError(reqId, err, ra); return; }
      sendSequence(listMessages(items, function (it) {
        return {
          ItemId: cut(it.id, MAX.ID),
          Title: cut(it.title, MAX.TITLE),
          Subtitle: cut(it.subtitle, MAX.SUBTITLE),
          Kind: it.kind
        };
      }), reqId);
    });
  } else if (type === REQ.LIVE) {
    source.getLive(p.ServiceTypeId, p.PlanId, function (err, live, ra) {
      if (err) { sendError(reqId, err, ra); return; }
      sendSequence([{
        LiveState: live.state,
        LiveCurTitle: cut(live.cur, MAX.TITLE),
        LiveNextTitle: cut(live.next, MAX.TITLE),
        LiveElapsed: live.elapsed,
        LiveLength: live.length
      }], reqId);
    });
  } else if (type === REQ.REQUESTS) {
    source.getRequests(cap, function (err, rows, ra) {
      if (err) { sendError(reqId, err, ra); return; }
      sendSequence(listMessages(rows, function (r) {
        return {
          ItemId: cut(r.id, MAX.ID),
          Title: cut(r.title, MAX.TITLE),
          Subtitle: cut(r.subtitle, MAX.SUBTITLE)
        };
      }), reqId);
    });
  } else if (type === REQ.RESPOND) {
    source.respond(p.ItemId, !!p.Accept, function (err, ra) {
      if (err) { sendError(reqId, err, ra); return; }
      sendSequence([{ Result: 1 }], reqId);
    });
  }
}

Pebble.addEventListener('ready', function () {
  console.log('PebbleKit JS ready' + (config.DEMO_MODE ? ' (demo mode)' : ''));
  sendSequence([{
    JSReady: 1,
    AuthState: auth.isSignedIn() ? 1 : 0,
    VibeOnChange: localStorage.getItem(K_VIBE) === '1' ? 1 : 0
  }], -1);
});

Pebble.addEventListener('appmessage', function (e) {
  var p = e.payload || {};
  if (p.Request) handleRequest(p);
});

// Opens the sign-in page (signed out) or the account page (signed in).
// Pebble.openURL() must run inside this handler, within 10 seconds.
Pebble.addEventListener('showConfiguration', function () {
  if (config.DEMO_MODE || !auth.isConfigured()) {
    var html = '<html><body style="font-family:sans-serif;padding:20px">' +
      '<h3>Not set up yet</h3><p>' +
      (config.DEMO_MODE ? 'This build runs in demo mode.' :
        'This build has no Planning Center client ID or login page URL. Set them in src/pkjs/config.js.') +
      '</p></body></html>';
    Pebble.openURL('data:text/html,' + encodeURIComponent(html));
    return;
  }
  var mode = auth.isSignedIn() ? 'manage' : 'signin';
  Pebble.openURL(auth.buildLoginUrl(mode, localStorage.getItem(K_VIBE) === '1'));
});

// The login page returns JSON through pebblejs://close#<data>.
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  var data;
  try { data = JSON.parse(e.response); } catch (err) {
    try { data = JSON.parse(decodeURIComponent(e.response)); } catch (err2) { return; }
  }
  if (!data || typeof data !== 'object') return;

  if (typeof data.vibe === 'boolean') {
    localStorage.setItem(K_VIBE, data.vibe ? '1' : '0');
    sendVibeSetting();
  }

  if (data.action === 'auth' && data.code) {
    auth.exchangeCode(String(data.code), String(data.state || ''), function (err) {
      if (err) {
        Pebble.showSimpleNotificationOnPebble('Sign-in failed',
          err === ERR.NETWORK ? 'No internet connection. Try again.' : 'Planning Center did not accept the sign-in. Try again.');
      }
      sendAuthState();
    });
  } else if (data.action === 'signout') {
    auth.signOut(function () { sendAuthState(); });
  } else if (data.action === 'error') {
    console.log('Sign-in cancelled or denied');
  }
});
