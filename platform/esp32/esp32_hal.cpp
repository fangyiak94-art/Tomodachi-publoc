#include "esp32_hal.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_random.h>
#include <time.h>

#include "board_pins.h"
#include "deskpet/alerts.h"
#include "deskpet/pack.h"
#include "lgfx_board.h"

namespace board {

// ---------------------------------------------------------------- display

void LcdDisplay::pushPixels(const dp::Rect& a, const dp::Color* px) {
  lcd_.pushImage(a.x, a.y, a.w, a.h, reinterpret_cast<const lgfx::rgb565_t*>(px));
}

void LcdDisplay::setBrightness(uint8_t level) { lcd_.setBrightness(level); }

bool TouchInput::readTouch(int16_t& x, int16_t& y) {
  uint16_t tx, ty;
  if (!lcd_.getTouch(&tx, &ty)) return false;
  x = static_cast<int16_t>(tx);
  y = static_cast<int16_t>(ty);
  return true;
}

bool TouchInput::readBack() { return digitalRead(DP_PIN_BOOT) == LOW; }

int64_t SystemClock::epoch() {
  time_t t = time(nullptr);
  return t > 1600000000 ? static_cast<int64_t>(t) : 0;  // not synced yet
}

// ---------------------------------------------------------------- storage

bool LittleFsStorage::read(const char* path, std::string& out, size_t maxBytes) {
  File f = LittleFS.open(path, "r");
  if (!f || f.isDirectory()) return false;
  size_t n = f.size();
  if (n > maxBytes) {
    f.close();
    return false;
  }
  out.resize(n);
  size_t got = n ? f.read(reinterpret_cast<uint8_t*>(&out[0]), n) : 0;
  f.close();
  return got == n;
}

bool LittleFsStorage::write(const char* path, const std::string& data) {
  File f = LittleFS.open(path, "w", true);
  if (!f) return false;
  size_t n = f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  f.close();
  return n == data.size();
}

bool LittleFsStorage::listDirs(const char* path, std::vector<std::string>& out) {
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return false;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) out.push_back(f.name());
  }
  return true;
}

void PiezoBuzzer::tone(uint16_t freqHz) {
  if (pin_ < 0) return;
  if (freqHz) ::tone(pin_, freqHz);
  else ::noTone(pin_);
}

// ---------------------------------------------------------------- secrets

bool Secrets::load() {
  File f = LittleFS.open("/secrets.json", "r");
  if (!f) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) return false;
  wifiSsid = doc["wifi_ssid"] | "";
  wifiPass = doc["wifi_pass"] | "";
  calendarUrl = doc["calendar_url"] | "";
  utcOffsetMinutes = doc["utc_offset_minutes"] | (8 * 60);
  return true;
}

// --------------------------------------------------------------- calendar

const char* WifiCalendar::status() const {
  if (s_.wifiSsid.isEmpty()) return "no cfg";
  switch (state_) {
    case State::Connecting: return "joining";
    case State::Fetching: return "sync";
    default: return "idle";
  }
}

void WifiCalendar::radioOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  state_ = State::Idle;
}

bool WifiCalendar::fetch(std::vector<dp::CalendarEvent>& out) {
  if (s_.calendarUrl.isEmpty()) return false;
  WiFiClientSecure client;
  // TODO before release: pin the Google root CA instead of skipping checks.
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);
  if (!http.begin(client, s_.calendarUrl)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    String body = http.getString();
    ok = dp::parseCalendarJson(body.c_str(), body.length(), out);
  }
  http.end();
  Serial.printf("calendar: HTTP %d, %s\n", code, ok ? "parsed" : "failed");
  return ok;
}

bool WifiCalendar::poll(std::vector<dp::CalendarEvent>& out) {
  const uint32_t now = ::millis();
  bool changed = false;

  if (!paused_ && !s_.wifiSsid.isEmpty()) {
    switch (state_) {
      case State::Idle:
        if (first_ || now - lastFetch_ >= kIntervalMs) {
          first_ = false;
          lastFetch_ = now;
          WiFi.mode(WIFI_STA);
          WiFi.begin(s_.wifiSsid.c_str(), s_.wifiPass.c_str());
          state_ = State::Connecting;
          stateSince_ = now;
        }
        break;
      case State::Connecting:
        if (WiFi.status() == WL_CONNECTED) {
          if (!ntpStarted_) {
            configTime(0, 0, "pool.ntp.org", "time.google.com");
            ntpStarted_ = true;
          }
          state_ = State::Fetching;
          stateSince_ = now;
        } else if (now - stateSince_ > kConnectTimeoutMs) {
          Serial.println("calendar: Wi-Fi join timed out");
          radioOff();
        }
        break;
      case State::Fetching:
        // Give NTP a few seconds on the first connection.
        if (time(nullptr) < 1600000000 && now - stateSince_ < 8000) break;
        if (fetch(remote_)) changed = true;
        radioOff();
        break;
    }
  }

  if (changed || dirty_) {
    dirty_ = false;
    out = remote_;
    out.insert(out.end(), local_.begin(), local_.end());
    return true;
  }
  return false;
}

// ---------------------------------------------------------- notifications

bool NotificationQueue::poll(dp::Notification& out) {
  bool got = false;
  portENTER_CRITICAL(&mux_);
  if (!q_.empty()) {
    out = q_.front();
    q_.pop_front();
    got = true;
  }
  portEXIT_CRITICAL(&mux_);
  return got;
}

void NotificationQueue::push(const dp::Notification& n) {
  portENTER_CRITICAL(&mux_);
  if (q_.size() >= 8) q_.pop_front();
  q_.push_back(n);
  portEXIT_CRITICAL(&mux_);
}

// ------------------------------------------------------------ upload portal

namespace {

WebServer* gServer = nullptr;
File gUpload;
String gUploadTmp, gUploadFinal, gUploadError, gLastFile;
size_t gUploadBytes = 0;

const char kPage[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Desk Pet</title>
<style>body{font-family:sans-serif;max-width:28em;margin:2em auto;padding:0 1em}
input,button{font-size:1em;margin:.3em 0;width:100%}#log{white-space:pre-wrap}</style></head>
<body><h2>Upload a pack</h2>
<p>Pick a pack id (a-z, 0-9, - _) and all its files: manifest.json plus any .dps images.</p>
<input id="id" placeholder="pack id, e.g. blobby">
<input id="files" type="file" multiple>
<button onclick="go()">Upload</button><div id="log"></div>
<script>
async function go(){const id=document.getElementById('id').value.trim();
const files=document.getElementById('files').files;const log=document.getElementById('log');
log.textContent='';for(const f of files){const fd=new FormData();fd.append('file',f,f.name);
const r=await fetch('/upload?pack='+encodeURIComponent(id),{method:'POST',body:fd});
log.textContent+=f.name+': '+await r.text()+'\n';}
log.textContent+='Done. Press BOOT on the pet, then pick the pack in Settings.';}
</script></body></html>)HTML";

void handleUploadData() {
  HTTPUpload& up = gServer->upload();
  if (up.status == UPLOAD_FILE_START) {
    gUploadError = "";
    gUploadBytes = 0;
    std::string id = gServer->arg("pack").c_str();
    std::string name = up.filename.c_str();
    if (!dp::isSafeFileName(id) || !dp::isSafeFileName(name)) {
      gUploadError = "bad pack id or file name";
      return;
    }
    String dir = String("/packs/") + id.c_str();
    gUploadFinal = dir + "/" + name.c_str();
    gUploadTmp = gUploadFinal + ".part";
    gUpload = LittleFS.open(gUploadTmp, "w", true);
    if (!gUpload) gUploadError = "cannot write";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!gUploadError.isEmpty() || !gUpload) return;
    gUploadBytes += up.currentSize;
    if (gUploadBytes > dp::kMaxSpriteBytes) {
      gUploadError = "file too large";
      gUpload.close();
      LittleFS.remove(gUploadTmp);
      return;
    }
    if (gUpload.write(up.buf, up.currentSize) != up.currentSize) gUploadError = "storage full";
  } else if (up.status == UPLOAD_FILE_END) {
    if (gUpload) gUpload.close();
    if (gUploadError.isEmpty()) {
      LittleFS.remove(gUploadFinal);
      LittleFS.rename(gUploadTmp, gUploadFinal);
      gLastFile = gUploadFinal;
    } else {
      LittleFS.remove(gUploadTmp);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (gUpload) gUpload.close();
    LittleFS.remove(gUploadTmp);
    gUploadError = "aborted";
  }
}

}  // namespace

void WifiPortal::start() {
  if (running_) return;
  cal_.setPaused(true);
  WiFi.disconnect(true);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char buf[24];
  snprintf(buf, sizeof(buf), "DeskPet-%02X%02X", mac[4], mac[5]);
  ssid_ = buf;
  snprintf(buf, sizeof(buf), "%08lu", static_cast<unsigned long>(esp_random() % 100000000UL));
  pass_ = buf;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_.c_str(), pass_.c_str());
  gServer = new WebServer(80);
  gServer->on("/", HTTP_GET, []() { gServer->send_P(200, "text/html", kPage); });
  gServer->on(
      "/upload", HTTP_POST,
      []() {
        if (gUploadError.isEmpty()) gServer->send(200, "text/plain", "ok");
        else gServer->send(400, "text/plain", gUploadError);
      },
      handleUploadData);
  gServer->begin();
  running_ = true;
  Serial.printf("portal: %s / %s at %s\n", ssid_.c_str(), pass_.c_str(),
                WiFi.softAPIP().toString().c_str());
}

void WifiPortal::stop() {
  if (!running_) return;
  gServer->stop();
  delete gServer;
  gServer = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  cal_.setPaused(false);
  running_ = false;
}

void WifiPortal::loop() {
  if (running_) gServer->handleClient();
}

std::vector<std::string> WifiPortal::statusLines() {
  if (!running_) return {"Starting..."};
  std::vector<std::string> lines = {std::string("WiFi ") + ssid_.c_str(),
                                    std::string("Pass ") + pass_.c_str(), "192.168.4.1"};
  if (!gLastFile.isEmpty()) {
    int slash = gLastFile.lastIndexOf('/');
    lines.push_back(std::string("Got ") + gLastFile.substring(slash + 1).c_str());
  }
  return lines;
}

}  // namespace board
