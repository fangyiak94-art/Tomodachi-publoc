/**
 * Desk Pet calendar feed (Google Apps Script web app).
 *
 * Returns the next events of the default calendar as
 *   {"events":[{"id","title","start","end"}]}   (start/end = unix seconds)
 * so the device never needs Google OAuth.
 *
 * Setup:
 *  1. script.google.com > New project, paste this file.
 *  2. Project Settings > Script properties: add TOKEN = a long random string.
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
  return json_({ events: events });
}

function json_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}
