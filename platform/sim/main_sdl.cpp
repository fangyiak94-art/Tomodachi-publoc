// Desktop simulator: the real engine in an SDL2 window shaped like the round
// 240x240 panel. Mouse = finger (click, double click, hold, drag to swipe).
//
// Keys:
//   arrows      swipe (the arrow is the finger direction)
//   space       tap the pet        enter  double tap      l  long press
//   esc/bksp    BOOT (back)
//   n           phone notification  m      meeting in 10 minutes
//   f           cycle speed 1x / 60x / 600x (pet time only)
//   w           cycle weather: clear, cloudy, rain, snow
//   h           jump the clock +3 hours (see day/night lighting)
//   1           zoom 1:1 (real panel size) / 2x / 3x
//   p           save screenshot     q      quit
//
// The room editor is served at http://localhost:8080 while Settings > UPLOAD
// is open (or while "EDIT" shows on the home screen).
//
// Extra pet folders (e.g. generated Pokemon packs) and a starting pet/room:
//   deskpet_sim --packs pokemon_tomo_packs/packs --pack gengar --room haunted
//
//   deskpet_sim [--edit] [--packs DIR]... [--pack ID] [--room ID] [--fs fs] [--state .deskpet-state] [--calendar mock/calendar.json] [--port 8080]
#include <SDL.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

#include "deskpet/engine.h"
#include "http_portal.h"
#include "pc_platform.h"

using namespace dp;

namespace {

class SdlClock : public Clock {
 public:
  uint32_t millis() override { return SDL_GetTicks(); }
  int64_t epoch() override { return static_cast<int64_t>(std::time(nullptr)) + skew; }
  int64_t skew = 0;  // "h" key moves the clock to preview day/night
  int32_t utcOffsetMinutes() override {
    std::time_t t = std::time(nullptr);
    std::tm local = *std::localtime(&t);
    std::tm utc = *std::gmtime(&t);
    int diff = (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
    int dayDiff = local.tm_yday - utc.tm_yday;
    if (dayDiff == 1 || dayDiff < -1) diff += 24 * 60;
    if (dayDiff == -1 || dayDiff > 1) diff -= 24 * 60;
    return diff;
  }
};

class SdlInput : public Input {
 public:
  int scale = 2;
  bool readTouch(int16_t& x, int16_t& y) override {
    int mx, my;
    uint32_t b = SDL_GetMouseState(&mx, &my);
    x = static_cast<int16_t>(mx / scale);
    y = static_cast<int16_t>(my / scale);
    return (b & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
  }
  bool readBack() override { return false; }
  bool pollGesture(InputEvent& out) override { return script.pollGesture(out); }
  pc::ScriptInput script;
};

// Square-wave buzzer through SDL audio.
class SdlBuzzer : public Buzzer {
 public:
  bool open() {
    SDL_AudioSpec want{}, have{};
    want.freq = 22050;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = &SdlBuzzer::callback;
    want.userdata = this;
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev_) return false;
    rate_ = have.freq;
    SDL_PauseAudioDevice(dev_, 0);
    return true;
  }
  void tone(uint16_t f) override { freq_ = f; }

 private:
  static void callback(void* self, Uint8* stream, int len) {
    auto* b = static_cast<SdlBuzzer*>(self);
    auto* out = reinterpret_cast<int16_t*>(stream);
    int n = len / 2;
    uint16_t f = b->freq_;
    for (int i = 0; i < n; ++i) {
      if (f == 0) {
        out[i] = 0;
        continue;
      }
      b->phase_ += static_cast<double>(f) / b->rate_;
      if (b->phase_ >= 1.0) b->phase_ -= 1.0;
      out[i] = b->phase_ < 0.5 ? 2500 : -2500;
    }
  }
  SDL_AudioDeviceID dev_ = 0;
  int rate_ = 22050;
  double phase_ = 0;
  std::atomic<uint16_t> freq_{0};
};

}  // namespace

int main(int argc, char** argv) {
  std::string fsRoot = "fs", state = ".deskpet-state", calendarPath = "mock/calendar.json";
  int port = 8080;
  bool edit = false;
  std::vector<std::string> packDirs;
  std::string startPack, startRoom;
  for (int i = 1; i < argc; ++i) {
    auto value = [&](const char* flag) { return !std::strcmp(argv[i], flag) && i + 1 < argc; };
    if (!std::strcmp(argv[i], "--edit")) edit = true;  // open the room editor at start
    else if (value("--fs")) fsRoot = argv[++i];
    else if (value("--state")) state = argv[++i];
    else if (value("--calendar")) calendarPath = argv[++i];
    else if (value("--port")) port = std::atoi(argv[++i]);
    else if (value("--packs")) packDirs.push_back(argv[++i]);  // extra pet folder, repeatable
    else if (value("--pack")) startPack = argv[++i];
    else if (value("--room")) startRoom = argv[++i];
    else {
      std::fprintf(stderr, "unknown argument %s\n", argv[i]);
      return 1;
    }
  }
  if (!std::filesystem::exists(fsRoot + "/packs")) {
    std::fprintf(stderr, "No packs in '%s/packs'. Run from the repo root or pass --fs.\n",
                 fsRoot.c_str());
    return 1;
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  int zoom = 2;
  SDL_Window* win = SDL_CreateWindow("Desk Pet simulator", SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED, kScreenW * zoom, kScreenH * zoom, 0);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
  SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                       kScreenW, kScreenH);

  pc::FramebufferDisplay display;
  SdlInput input;
  input.scale = zoom;
  SdlClock clock;
  pc::DirStorage storage(fsRoot);
  std::filesystem::create_directories(state);
  storage.setWriteRoot(state);
  for (const std::string& d : packDirs) {
    if (!std::filesystem::is_directory(d)) {
      std::fprintf(stderr, "--packs %s: not a folder\n", d.c_str());
      return 1;
    }
    storage.addPackDir(d);
  }
  SdlBuzzer buzzer;
  bool audio = buzzer.open();
  pc::FileCalendar calendar(calendarPath, &clock);
  pc::QueueNotifications notifications;
  pc::MockWeather weather;
  calendar.setWeatherSink(&weather);
  pc::HttpPortal portal(storage, port);

  Platform p;
  p.display = &display;
  p.input = &input;
  p.clock = &clock;
  p.storage = &storage;
  p.buzzer = audio ? &buzzer : nullptr;
  p.calendar = &calendar;
  p.notifications = &notifications;
  p.weather = &weather;
  p.uploader = &portal;
  p.log = [](const char* l) { std::printf("%s\n", l); };
  p.connectivity = []() { return ConnectivityStatus{"sim", "sim"}; };
  p.seed = static_cast<uint32_t>(std::time(nullptr));

  Engine engine(p);
  engine.begin();
  if (!startPack.empty() && !engine.selectPack(startPack)) {
    std::fprintf(stderr, "pet '%s': %s\n", startPack.c_str(), engine.lastPackError().c_str());
    return 1;
  }
  if (!startRoom.empty() && !engine.selectRoom(startRoom)) {
    std::fprintf(stderr, "room '%s': %s\n", startRoom.c_str(), engine.lastRoomError().c_str());
    return 1;
  }
  if (edit) engine.openPortal();
  std::printf("Desk Pet simulator. Mouse = finger. Keys: arrows swipe, space tap, enter double tap,\n"
              "l long press, esc BOOT, n notification, m meeting, w weather, h +3h, f speed, 1 zoom,\n"
              "p screenshot, q quit. Room editor: Settings > UPLOAD, then http://localhost:%d\n", port);

  static const Notification samples[] = {
      {"WhatsApp", "Ali", "Lunch at 1? The usual place"},
      {"Gmail", "Invoice #2041", "Your invoice is ready"},
      {"Slack", "#desk-pet", "Pack upload works on the board!"},
  };
  int sample = 0, shots = 0;
  const float speeds[] = {1.0f, 60.0f, 600.0f};
  int speed = 0;
  bool running = true;
  uint32_t lastTitle = 0;

  std::vector<Color> masked(kScreenW * kScreenH);
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type != SDL_KEYDOWN || ev.key.repeat) continue;
      auto& q = input.script;
      switch (ev.key.keysym.sym) {
        case SDLK_q: running = false; break;
        case SDLK_LEFT: q.push(Gesture::SwipeLeft); break;
        case SDLK_RIGHT: q.push(Gesture::SwipeRight); break;
        case SDLK_UP: q.push(Gesture::SwipeUp); break;
        case SDLK_DOWN: q.push(Gesture::SwipeDown); break;
        case SDLK_SPACE: q.push(Gesture::Tap); break;
        case SDLK_RETURN: q.push(Gesture::DoubleTap); break;
        case SDLK_l: q.push(Gesture::LongPress); break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE: q.push(Gesture::Back); break;
        case SDLK_n: notifications.queue.push_back(samples[sample++ % 3]); break;
        case SDLK_m: {
          int64_t now = clock.epoch();
          calendar.addEvent({"sim-" + std::to_string(now), "Quick sync", now + 600, now + 1800});
          break;
        }
        case SDLK_f:
          speed = (speed + 1) % 3;
          engine.setTimeScale(speeds[speed]);
          std::printf("pet time x%.0f\n", speeds[speed]);
          break;
        case SDLK_w: {
          static const Weather cycle[] = {Weather::Clear, Weather::Cloudy, Weather::Rain, Weather::Snow};
          static int wi = 0;
          weather.set(cycle[wi++ % 4]);
          break;
        }
        case SDLK_h:
          clock.skew += 3 * 3600;
          std::printf("clock +3h\n");
          break;
        case SDLK_1:
          zoom = zoom % 3 + 1;
          input.scale = zoom;
          SDL_SetWindowSize(win, kScreenW * zoom, kScreenH * zoom);
          break;
        case SDLK_p: {
          std::string path = state + "/shot" + std::to_string(shots++) + ".ppm";
          display.savePpm(path);
          std::printf("saved %s\n", path.c_str());
          break;
        }
        default: break;
      }
    }

    engine.loop();

    if (display.dirty) {
      display.dirty = false;
      // Round panel mask and backlight.
      const int b = display.brightness;
      for (int y = 0; y < kScreenH; ++y) {
        for (int x = 0; x < kScreenW; ++x) {
          int dx = 2 * x - kScreenW + 1, dy = 2 * y - kScreenH + 1;
          Color c = display.fb[y * kScreenW + x];
          if (dx * dx + dy * dy > kScreenW * kScreenW) c = 0;
          else if (b < 255) {
            int r = ((c >> 11) & 31) * b / 255, g = ((c >> 5) & 63) * b / 255, bl = (c & 31) * b / 255;
            c = static_cast<Color>((r << 11) | (g << 5) | bl);
          }
          masked[y * kScreenW + x] = c;
        }
      }
      SDL_UpdateTexture(tex, nullptr, masked.data(), kScreenW * 2);
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);

    uint32_t now = SDL_GetTicks();
    if (now - lastTitle > 500) {
      lastTitle = now;
      const PetStats& s = engine.pet().stats();
      char title[160];
      std::snprintf(title, sizeof(title),
                    "Desk Pet  %s  food %.0f fun %.0f nrg %.0f xp %u  [%s]  x%.0f",
                    moodName(engine.pet().mood()), s.food, s.fun, s.energy, s.xp,
                    screenName(engine.screen()), engine.timeScale());
      SDL_SetWindowTitle(win, title);
    }
    SDL_Delay(5);
  }
  engine.save();
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
