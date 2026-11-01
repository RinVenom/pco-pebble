// Per-phone settings kept in localStorage.
//
// - Service types: the list seen on the last plans refresh ([[id, name], ...]),
//   so the settings page can show it without waiting for the network.
// - Hidden service types: ids the user unchecked on the settings page. Hidden
//   rather than shown, so a service type added later appears by default.

var K_TYPES = 'pco.serviceTypes';
var K_HIDDEN = 'pco.hiddenTypes';
var MAX_TYPES = 100;

function readJson(key) {
  try {
    var v = JSON.parse(localStorage.getItem(key) || 'null');
    return Array.isArray(v) ? v : [];
  } catch (e) {
    return [];
  }
}

function cleanId(id) {
  id = String(id);
  return /^[0-9A-Za-z_-]{1,32}$/.test(id) ? id : null;
}

function getServiceTypes() {
  return readJson(K_TYPES);
}

// types: [{id, name}] from the API.
function setServiceTypes(types) {
  var list = [];
  types.forEach(function (t) {
    var id = cleanId(t.id);
    if (id && list.length < MAX_TYPES) list.push([id, String(t.name || 'Services').substring(0, 60)]);
  });
  localStorage.setItem(K_TYPES, JSON.stringify(list));
}

function getHiddenTypes() {
  return readJson(K_HIDDEN).map(String);
}

// Saves the hidden ids. Returns true if the set changed.
function setHiddenTypes(ids) {
  var clean = [];
  (Array.isArray(ids) ? ids : []).forEach(function (id) {
    var c = cleanId(id);
    if (c && clean.indexOf(c) < 0 && clean.length < MAX_TYPES) clean.push(c);
  });
  clean.sort();
  var before = getHiddenTypes().slice().sort();
  localStorage.setItem(K_HIDDEN, JSON.stringify(clean));
  return JSON.stringify(before) !== JSON.stringify(clean);
}

function isHidden(id) {
  return getHiddenTypes().indexOf(String(id)) >= 0;
}

// Account data: removed on sign-out.
function clearAccount() {
  localStorage.removeItem(K_TYPES);
  localStorage.removeItem(K_HIDDEN);
}

module.exports = {
  getServiceTypes: getServiceTypes,
  setServiceTypes: setServiceTypes,
  getHiddenTypes: getHiddenTypes,
  setHiddenTypes: setHiddenTypes,
  isHidden: isHidden,
  clearAccount: clearAccount
};
