// Canned data used only when config.DEMO_MODE is true. Same interface as
// pco.js. Lets the emulator show every screen without a Planning Center
// account. Never enable in a release build.

var prefs = require('./prefs');

var KIND = { ITEM: 0, SONG: 1, HEADER: 2, MEDIA: 3 };

var PLANS = [
  { section: 'Sunday Morning', serviceTypeId: '101', planId: '9001', title: 'Nov 1', subtitle: 'Gratitude, Week 1', status: 2 },
  { section: 'Sunday Morning', serviceTypeId: '101', planId: '9002', title: 'Nov 8', subtitle: 'Gratitude, Week 2', status: 1 },
  { section: 'Sunday Morning', serviceTypeId: '101', planId: '9003', title: 'Nov 15', subtitle: 'Gratitude, Week 3', status: 0 },
  { section: 'Youth Night', serviceTypeId: '102', planId: '9101', title: 'Nov 4', subtitle: 'Worship night', status: 3 },
  { section: 'Youth Night', serviceTypeId: '102', planId: '9102', title: 'Nov 11', subtitle: '', status: 1 }
];
// Start times relative to now, so the app glance has something upcoming.
var DAY = 86400;
var nowSec = Math.floor(Date.now() / 1000);
[0.1, 7, 14, 3, 10].forEach(function (days, i) { PLANS[i].time = nowSec + Math.round(days * DAY); });

var ITEMS = [
  { id: '1', title: 'Pre-Service', kind: KIND.HEADER, length: 0, subtitle: '' },
  { id: '2', title: 'Countdown Video', kind: KIND.MEDIA, length: 300, subtitle: 'Pre-service · Media · 5:00' },
  { id: '3', title: 'Worship', kind: KIND.HEADER, length: 0, subtitle: '' },
  { id: '4', title: 'Come Thou Fount', kind: KIND.SONG, length: 270, subtitle: 'Key G · 4:30' },
  { id: '5', title: 'Welcome & Announcements', kind: KIND.ITEM, length: 240, subtitle: '4:00' },
  { id: '6', title: 'Great Is Thy Faithfulness', kind: KIND.SONG, length: 300, subtitle: 'Key D · 5:00' },
  { id: '7', title: 'Message', kind: KIND.HEADER, length: 0, subtitle: '' },
  { id: '8', title: 'Sermon: Give Thanks', kind: KIND.ITEM, length: 1800, subtitle: '30:00' },
  { id: '9', title: 'Closing Prayer', kind: KIND.ITEM, length: 120, subtitle: '2:00' }
];

var REQUESTS = [
  { id: '7001', planId: '9002', title: 'Sunday Morning', subtitle: 'Nov 8 · Acoustic Guitar' },
  { id: '7002', planId: '9102', title: 'Youth Night', subtitle: 'Nov 11 · Sound' }
];

// The demo service started 95 seconds into "Come Thou Fount" when JS loaded.
var demoStart = Date.now() - 95 * 1000;

function later(fn) { setTimeout(fn, 300); }

module.exports = {
  getPlans: function (last, cap, cb) {
    later(function () {
      prefs.setServiceTypes([{ id: '101', name: 'Sunday Morning' }, { id: '102', name: 'Youth Night' }]);
      cb(0, PLANS.filter(function (p) { return !prefs.isHidden(p.serviceTypeId); }).slice(0, cap));
    });
  },
  getItems: function (st, plan, cap, cb) {
    later(function () { cb(0, ITEMS.slice(0, cap)); });
  },
  getLive: function (st, plan, cb) {
    later(function () {
      // Walk the non-header items using their lengths.
      var t = Math.floor((Date.now() - demoStart) / 1000);
      var playable = ITEMS.filter(function (i) { return i.kind !== KIND.HEADER && i.id !== '2'; });
      var idx = 0;
      while (idx < playable.length - 1 && t >= playable[idx].length) { t -= playable[idx].length; idx++; }
      cb(0, {
        state: 1,
        cur: playable[idx].title,
        next: idx + 1 < playable.length ? playable[idx + 1].title : '',
        elapsed: t,
        length: playable[idx].length
      });
    });
  },
  getRequests: function (cap, cb) {
    later(function () { cb(0, REQUESTS.slice(0, cap)); });
  },
  respond: function (id, accept, cb) {
    later(function () {
      REQUESTS = REQUESTS.filter(function (r) {
        if (r.id !== id) return true;
        PLANS.forEach(function (p) { if (p.planId === r.planId) p.status = accept ? 2 : 3; });
        return false;
      });
      cb(0);
    });
  }
};
