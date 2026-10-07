/**
 * Desk Pet calendar feed (Google Apps Script web app).
 *
 * Returns the next events of the default calendar, plus the current weather:
 *   {"events":[{"id","title","start","end"}], "weather":"clear|cloudy|rain|snow"}
 * (start/end = unix seconds) so the device never needs Google OAuth.
 *
 * Setup:
 *  1. script.google.com > New project, paste this file.
 *  2. Project Settings > Script properties: add TOKEN = a long random string.
 *     Optional, for weather: LAT and LON (e.g. 3.139 and 101.687 for KL).
 *     Weather comes from Open-Meteo (free, no key) and is cached 15 minutes.
 *  3. Deploy > New deployment > Web app; Execute as: Me; Who has access: Anyone.
 *  4. Put "<web app URL>?token=<TOKEN>" in fs/secrets.json as calendar_url.
 * The URL + token is a secret: anyone holding it can read your next events'
 * titles. Rotate TOKEN if the device is lost.
 */
function doGet(e) {
  var token = PropertiesService.getScriptProperties().getProperty('TOKEN');
  if (!token || !e || !e.parameter || e.parameter.token !== token) {
    return json_({ error: 'forbidden' });
  }
  var now = new Date();
  var until = new Date(now.getTime() + 24 * 3600 * 1000);
  var events = CalendarApp.getDefaultCalendar()
    .getEvents(now, until)
    .filter(function (ev) { return !ev.isAllDayEvent(); })
    .slice(0, 8)
    .map(function (ev) {
      return {
        id: ev.getId().slice(0, 40),
        title: ev.getTitle().slice(0, 64),
        start: Math.floor(ev.getStartTime().getTime() / 1000),
        end: Math.floor(ev.getEndTime().getTime() / 1000),
      };
    });
  var out = { events: events };
  var weather = weather_();
  if (weather) out.weather = weather;
  return json_(out);
}

// WMO weather code -> the four kinds the pet's room understands.
function weather_() {
  var props = PropertiesService.getScriptProperties();
  var lat = props.getProperty('LAT'), lon = props.getProperty('LON');
  if (!lat || !lon) return null;
  var cache = CacheService.getScriptCache();
  var cached = cache.get('weather');
  if (cached) return cached;
  try {
    var url = 'https://api.open-meteo.com/v1/forecast?current=weather_code&latitude=' +
        encodeURIComponent(lat) + '&longitude=' + encodeURIComponent(lon);
    var code = JSON.parse(UrlFetchApp.fetch(url).getContentText()).current.weather_code;
    var kind = code <= 1 ? 'clear'
        : code <= 48 ? 'cloudy'
        : (code >= 71 && code <= 77) || code == 85 || code == 86 ? 'snow'
        : 'rain';  // drizzle, rain, showers, thunderstorms
    cache.put('weather', kind, 15 * 60);
    return kind;
  } catch (e) {
    return null;
  }
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
