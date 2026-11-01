// Planning Center Services API calls. Each function returns flat rows
// that index.js sends to the watch one message at a time.
// Docs: https://developer.planning.center/docs/#/apps/services

var auth = require('./auth');
var http = require('./http');
var prefs = require('./prefs');

var BASE = 'https://api.planningcenteronline.com/services/v2';
var ERR = auth.ERR;
var PLANS_PER_TYPE = 4;

// Item kinds shared with the watch (see data.h).
var KIND = { ITEM: 0, SONG: 1, HEADER: 2, MEDIA: 3 };

// Items per plan, kept for the life of this JS context so live polls can
// turn item ids into titles without another request.
var itemsCache = {};

// cb(err, json, retryAfter). Refreshes the token once on HTTP 401.
function api(method, path, body, cb, isRetry) {
  auth.getToken(function (err, token) {
    if (err) { cb(err); return; }
    var headers = { 'Authorization': 'Bearer ' + token };
    if (body) headers['Content-Type'] = 'application/json';
    var url = path.indexOf('http') === 0 ? path : BASE + path;
    http.request(method, url, headers, body ? JSON.stringify(body) : null, function (res) {
      if (res.status >= 200 && res.status < 300) { cb(0, res.json); return; }
      if (res.status === 401 && !isRetry) {
        auth.refresh(function (rErr) {
          if (rErr) { cb(rErr); return; }
          api(method, path, body, cb, true);
        });
        return;
      }
      console.log('API ' + method + ' failed, HTTP ' + res.status);
      if (res.status === 0) cb(ERR.NETWORK);
      else if (res.status === 401 || res.status === 403) cb(ERR.AUTH);
      else if (res.status === 404) cb(ERR.NOT_FOUND);
      else if (res.status === 429) cb(ERR.RATE_LIMIT, null, res.retryAfter);
      else cb(ERR.SERVER);
    });
  });
}

// Runs tasks (functions taking a done callback) with at most `limit` at once.
function runLimited(tasks, limit, done) {
  var next = 0, running = 0, finished = 0;
  if (!tasks.length) { done(); return; }
  function launch() {
    while (running < limit && next < tasks.length) {
      running++;
      tasks[next++](function () {
        running--; finished++;
        if (finished === tasks.length) done(); else launch();
      });
    }
  }
  launch();
}

function startOfTodayIso() {
  var d = new Date();
  d.setHours(0, 0, 0, 0);
  return d.toISOString();
}

// Upcoming plans grouped by service type. The service type the user opened
// last comes first. "after midnight today" keeps a service that is already
// running in the list, which filter=future may drop once it starts.
// Service types hidden on the settings page are skipped, and their plans
// are never requested.
function getPlans(lastServiceTypeId, cap, cb) {
  api('GET', '/service_types?per_page=100', null, function (err, json, retryAfter) {
    if (err) { cb(err, null, retryAfter); return; }
    var allTypes = (json && json.data) || [];
    prefs.setServiceTypes(allTypes.map(function (t) {
      return { id: t.id, name: t.attributes && t.attributes.name };
    }));
    var types = allTypes.filter(function (t) { return !prefs.isHidden(t.id); });
    var byType = {};
    var firstErr = 0, firstRetry = 0;
    var after = encodeURIComponent(startOfTodayIso());
    var tasks = types.map(function (t) {
      return function (done) {
        api('GET', '/service_types/' + t.id + '/plans?filter=after&after=' + after +
            '&order=sort_date&per_page=' + PLANS_PER_TYPE, null, function (e, pj, ra) {
          if (e) { if (!firstErr) { firstErr = e; firstRetry = ra; } }
          else byType[t.id] = (pj && pj.data) || [];
          done();
        });
      };
    });
    runLimited(tasks, 4, function () {
      // Show what loaded. Fail only if nothing did.
      if (firstErr && Object.keys(byType).length === 0) { cb(firstErr, null, firstRetry); return; }
      var ordered = types.slice();
      ordered.sort(function (a, b) {
        if (a.id === lastServiceTypeId) return -1;
        if (b.id === lastServiceTypeId) return 1;
        return 0;
      });
      var rows = [];
      ordered.forEach(function (t) {
        (byType[t.id] || []).forEach(function (p) {
          if (rows.length >= cap) return;
          var a = p.attributes || {};
          rows.push({
            section: (t.attributes && t.attributes.name) || 'Services',
            serviceTypeId: String(t.id),
            planId: String(p.id),
            title: a.short_dates || a.dates || 'No date',
            subtitle: a.title || a.series_title || (a.items_count ? a.items_count + ' items' : '')
          });
        });
      });
      cb(0, rows);
    });
  });
}

function formatLength(sec) {
  if (!sec || sec <= 0) return '';
  var m = Math.floor(sec / 60), s = sec % 60;
  return m + ':' + (s < 10 ? '0' : '') + s;
}

function normalizeItem(it) {
  var a = it.attributes || {};
  var kind = KIND.ITEM;
  if (a.item_type === 'song') kind = KIND.SONG;
  else if (a.item_type === 'header') kind = KIND.HEADER;
  else if (a.item_type === 'media') kind = KIND.MEDIA;
  var parts = [];
  if (a.service_position === 'pre') parts.push('Pre-service');
  else if (a.service_position === 'post') parts.push('Post-service');
  if (kind === KIND.SONG && a.key_name) parts.push('Key ' + a.key_name);
  if (kind === KIND.MEDIA) parts.push('Media');
  var len = formatLength(a.length);
  if (len) parts.push(len);
  return {
    id: String(it.id),
    title: a.title || '(untitled)',
    kind: kind,
    length: a.length || 0,
    subtitle: parts.join(' · ')
  };
}

// Plan order. Follows `links.next` until `cap` items are loaded.
function getItems(serviceTypeId, planId, cap, cb) {
  var items = [];
  function page(path) {
    api('GET', path, null, function (err, json, retryAfter) {
      if (err) { cb(err, null, retryAfter); return; }
      ((json && json.data) || []).forEach(function (it) {
        if (items.length < cap) items.push(normalizeItem(it));
      });
      var next = json && json.links && json.links.next;
      if (next && items.length < cap) page(next);
      else { itemsCache[planId] = items; cb(0, items); }
    });
  }
  page('/service_types/' + serviceTypeId + '/plans/' + planId + '/items?per_page=100&order=sequence');
}

function findIncluded(json, ref) {
  if (!ref || !json || !json.included) return null;
  for (var i = 0; i < json.included.length; i++) {
    var inc = json.included[i];
    if (inc.type === ref.type && String(inc.id) === String(ref.id)) return inc;
  }
  return null;
}

function itemIdOf(itemTime) {
  var rel = itemTime && itemTime.relationships && itemTime.relationships.item;
  return rel && rel.data ? String(rel.data.id) : null;
}

function findItem(items, id) {
  for (var i = 0; i < items.length; i++) if (items[i].id === id) return i;
  return -1;
}

// Live state for a plan. Live links to the current and next ItemTime;
// each ItemTime has live_start_at, length and its Item.
// Result: { state: 0 not live / 1 live, cur, next, elapsed, length }.
function getLive(serviceTypeId, planId, cb, refetched) {
  var items = itemsCache[planId];
  if (!items) {
    getItems(serviceTypeId, planId, 100, function (err, list, ra) {
      if (err) { cb(err, null, ra); return; }
      getLive(serviceTypeId, planId, cb, true);
    });
    return;
  }
  api('GET', '/service_types/' + serviceTypeId + '/plans/' + planId +
      '/live?include=current_item_time,next_item_time', null, function (err, json, retryAfter) {
    if (err) { cb(err, null, retryAfter); return; }
    var live = json && json.data;
    if (live && live.length !== undefined) live = live[0]; // tolerate a list response
    var rel = (live && live.relationships) || {};
    var cur = findIncluded(json, rel.current_item_time && rel.current_item_time.data);
    var nxt = findIncluded(json, rel.next_item_time && rel.next_item_time.data);

    // Fallback if relationships are missing: the started, unfinished ItemTime.
    if (!cur && json && json.included) {
      json.included.forEach(function (inc) {
        var a = inc.attributes || {};
        if (inc.type === 'ItemTime' && a.live_start_at && !a.live_end_at) cur = inc;
      });
    }

    var curId = itemIdOf(cur);
    var nextId = itemIdOf(nxt);
    if ((curId && findItem(items, curId) < 0) || (nextId && findItem(items, nextId) < 0)) {
      if (!refetched) {
        // The plan changed since the items were loaded.
        delete itemsCache[planId];
        getLive(serviceTypeId, planId, cb, true);
        return;
      }
    }

    var result = { state: 0, cur: '', next: '', elapsed: 0, length: 0 };
    var startedAt = cur && cur.attributes && cur.attributes.live_start_at;
    if (cur && startedAt) {
      var ci = findItem(items, curId);
      result.state = 1;
      result.cur = ci >= 0 ? items[ci].title : 'Current item';
      result.elapsed = Math.max(0, Math.floor((Date.now() - Date.parse(startedAt)) / 1000));
      result.length = (cur.attributes.length) || (ci >= 0 ? items[ci].length : 0) || 0;
      var ni = findItem(items, nextId);
      if (ni < 0 && ci >= 0) {
        // No next ItemTime: use the next non-header item in the order.
        for (var j = ci + 1; j < items.length; j++) {
          if (items[j].kind !== KIND.HEADER) { ni = j; break; }
        }
      }
      result.next = ni >= 0 ? items[ni].title : '';
    } else {
      // Not started: show the first item as next.
      for (var k = 0; k < items.length; k++) {
        if (items[k].kind !== KIND.HEADER) { result.next = items[k].title; break; }
      }
    }
    cb(0, result);
  });
}

function getPersonId(cb) {
  var cached = localStorage.getItem(auth.K_PERSON);
  if (cached) { cb(0, cached); return; }
  api('GET', '/me', null, function (err, json, ra) {
    if (err) { cb(err, null, ra); return; }
    var id = json && json.data && String(json.data.id);
    if (!id) { cb(ERR.SERVER); return; }
    localStorage.setItem(auth.K_PERSON, id);
    cb(0, id);
  });
}

// Pending scheduling requests (status "U", unconfirmed) for the signed-in person.
function getRequests(cap, cb) {
  getPersonId(function (err, pid, ra) {
    if (err) { cb(err, null, ra); return; }
    api('GET', '/people/' + pid + '/schedules?filter=future&per_page=50', null, function (e, json, ra2) {
      if (e) { cb(e, null, ra2); return; }
      var rows = [];
      ((json && json.data) || []).forEach(function (s) {
        var a = s.attributes || {};
        var status = String(a.status || '').toUpperCase();
        if (status.charAt(0) !== 'U' || rows.length >= cap) return;
        var bits = [a.short_dates || a.dates, a.team_position_name || a.team_name].filter(Boolean);
        rows.push({
          id: String(s.id),
          title: a.service_type_name || 'Request',
          subtitle: bits.join(' · ')
        });
      });
      cb(0, rows);
    });
  });
}

// Accept or decline one request. cb(err).
function respond(scheduleId, accept, cb) {
  getPersonId(function (err, pid, ra) {
    if (err) { cb(err, ra); return; }
    var path = '/people/' + pid + '/schedules/' + scheduleId + (accept ? '/accept' : '/decline');
    var body = accept ? {} : { data: { type: 'ScheduleDecline', attributes: { reason: '' } } };
    api('POST', path, body, function (e, json, ra2) { cb(e, ra2); });
  });
}

module.exports = {
  getPlans: getPlans,
  getItems: getItems,
  getLive: getLive,
  getRequests: getRequests,
  respond: respond,
  KIND: KIND
};
