// Desk Pet firmware entry point for the DIYMORE ESP32-C3 round board.
// Everything interesting lives in the portable engine; this file only wires
// the board's HAL implementations into it.
#include <Arduino.h>
#include <LittleFS.h>
#include <sys/time.h>

#include "board_pins.h"
#include "deskpet/engine.h"
#include "deskpet/room.h"
#include "esp32_hal.h"
#include "lgfx_board.h"

#ifdef DP_WITH_CHRONOS
#include <ChronosESP32.h>
#endif

namespace {

LGFX lcd;
board::LcdDisplay display(lcd);
board::TouchInput input(lcd);
board::SystemClock sysClock;
board::LittleFsStorage storage;
board::PiezoBuzzer buzzer(DP_PIN_BUZZER);
board::Secrets secrets;
board::WifiCalendar calendar(secrets);
board::FeedWeather weather(calendar);
board::NotificationQueue notifications;
board::WifiPortal portal(calendar);
dp::Engine* engine = nullptr;

#ifdef DP_WITH_CHRONOS
ChronosESP32 watch("DeskPet");
bool phoneConnected = false;
#endif

void logLine(const char* line) { Serial.println(line); }

dp::ConnectivityStatus connectivity() {
  dp::ConnectivityStatus s;
  s.wifi = calendar.status();
#ifdef DP_WITH_CHRONOS
  s.phone = phoneConnected ? "on" : "off";
#else
  s.phone = "serial";
#endif
  return s;
}

// Serial console for bring-up without a phone or calendar:
//   notify App|Title|Body     meeting <minutes> <title>
//   time <unix seconds>       weather rain   speed <x>  stats  help
String lineBuf;
void handleCommand(const String& cmd) {
  if (cmd.startsWith("notify ")) {
    String rest = cmd.substring(7);
    int a = rest.indexOf('|'), b = rest.indexOf('|', a + 1);
    dp::Notification n;
    n.app = rest.substring(0, a < 0 ? rest.length() : a).c_str();
    if (a >= 0) n.title = rest.substring(a + 1, b < 0 ? rest.length() : b).c_str();
    if (b >= 0) n.body = rest.substring(b + 1).c_str();
    notifications.push(n);
  } else if (cmd.startsWith("meeting ")) {
    int64_t now = sysClock.epoch();
    if (!now) {
      Serial.println("set the time first (time <unix seconds>)");
      return;
    }
    String rest = cmd.substring(8);
    int sp = rest.indexOf(' ');
    long mins = rest.substring(0, sp < 0 ? rest.length() : sp).toInt();
    dp::CalendarEvent e;
    e.id = "serial-" + std::to_string(static_cast<long long>(now));
    e.title = sp < 0 ? "Meeting" : rest.substring(sp + 1).c_str();
    e.start = now + mins * 60;
    e.end = e.start + 30 * 60;
    calendar.addLocal(e);
  } else if (cmd.startsWith("time ")) {
    struct timeval tv = {static_cast<time_t>(atoll(cmd.substring(5).c_str())), 0};
    settimeofday(&tv, nullptr);
  } else if (cmd.startsWith("weather ")) {
    dp::Weather w;
    if (dp::weatherFromName(cmd.substring(8).c_str(), w)) calendar.setWeather(w);
    else Serial.println("weather clear|cloudy|rain|snow");
  } else if (cmd == "edit") {
    engine->openPortal();
  } else if (cmd.startsWith("speed ")) {
    engine->setTimeScale(cmd.substring(6).toFloat());
  } else if (cmd == "stats") {
    Serial.println(engine->pet().toJson(sysClock.epoch()).c_str());
    Serial.printf("heap free %u, min %u, screen %s\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  dp::screenName(engine->screen()));
  } else {
    Serial.println("commands: notify App|Title|Body, meeting <min> <title>, time <epoch>, "
                   "weather <clear|cloudy|rain|snow>, edit, speed <x>, stats");
  }
}

void pollSerial() {
  while (Serial.available()) {
    char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      lineBuf.trim();
      if (lineBuf.length()) handleCommand(lineBuf);
      lineBuf = "";
    } else if (lineBuf.length() < 200) {
      lineBuf += ch;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(DP_PIN_BOOT, INPUT_PULLUP);

  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed (formatted)");
  if (secrets.load()) sysClock.offsetMin = secrets.utcOffsetMinutes;
  else Serial.println("no /secrets.json: calendar disabled (see docs/HARDWARE.md)");

#ifdef DP_WITH_CHRONOS
  watch.setConnectionCallback([](bool c) { phoneConnected = c; });
  watch.setNotificationCallback([](Notification n) {
    dp::Notification d;
    d.app = n.app.c_str();
    d.title = n.title.c_str();
    d.body = n.message.c_str();
    notifications.push(d);
  });
  watch.begin();
#endif

  dp::Platform p;
  p.display = &display;
  p.input = &input;
  p.clock = &sysClock;
  p.storage = &storage;
  p.buzzer = &buzzer;
  p.calendar = &calendar;
  p.notifications = &notifications;
  p.weather = &weather;
  p.uploader = &portal;
  p.log = logLine;
  p.connectivity = connectivity;
  p.seed = esp_random();

  engine = new dp::Engine(p);
  engine->begin();
  Serial.printf("deskpet: heap free %u after start\n", ESP.getFreeHeap());
}

void loop() {
  pollSerial();
#ifdef DP_WITH_CHRONOS
  watch.loop();
#endif
  engine->loop();
  delay(5);
}
