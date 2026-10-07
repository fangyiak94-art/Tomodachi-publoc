#include "pc_platform.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <ArduinoJson.h>

#include "deskpet/alerts.h"
#include "deskpet/room.h"

namespace fs = std::filesystem;

namespace dp {
namespace pc {

FramebufferDisplay::FramebufferDisplay() : fb(kScreenW * kScreenH, 0) {}

void FramebufferDisplay::pushPixels(const Rect& a, const Color* px) {
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) fb[(a.y + y) * kScreenW + a.x + x] = px[y * a.w + x];
  pushedPixels += static_cast<uint64_t>(a.w) * a.h;
  ++pushes;
  dirty = true;
}

bool FramebufferDisplay::savePpm(const std::string& path, bool roundMask) const {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", kScreenW, kScreenH);
  for (int y = 0; y < kScreenH; ++y) {
    for (int x = 0; x < kScreenW; ++x) {
      Color c = fb[y * kScreenW + x];
      int dx = x - kScreenW / 2, dy = y - kScreenH / 2;
      bool inside = !roundMask || dx * dx + dy * dy <= (kScreenW / 2) * (kScreenW / 2);
      unsigned char rgb3[3] = {0, 0, 0};
      if (inside) {
        rgb3[0] = static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31);
        rgb3[1] = static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63);
        rgb3[2] = static_cast<unsigned char>((c & 31) * 255 / 31);
      }
      std::fwrite(rgb3, 1, 3, f);
    }
  }
  std::fclose(f);
  return true;
}

std::string DirStorage::resolve(const char* path, bool forWrite) const {
  std::string p = path ? path : "";
  if (p.find("..") != std::string::npos) return "";
  if (!forWrite && p.rfind("/packs/", 0) == 0) {
    for (const std::string& dir : packDirs_) {
      std::string candidate = dir + p.substr(6);  // keep "/<id>/<file>"
      if (fs::exists(candidate)) return candidate;
    }
  }
  const std::string& base = (forWrite || (!writeRoot_.empty() && fs::exists(writeRoot_ + p)))
                                ? (writeRoot_.empty() ? root_ : writeRoot_)
                                : root_;
  return base + p;
}

bool DirStorage::read(const char* path, std::string& out, size_t maxBytes) {
  std::string full = resolve(path, false);
  if (full.empty()) return false;
  std::error_code ec;
  auto size = fs::file_size(full, ec);
  if (ec || size > maxBytes) return false;
  std::ifstream in(full, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool DirStorage::write(const char* path, const std::string& data) {
  std::string full = resolve(path, true);
  if (full.empty()) return false;
  std::error_code ec;
  fs::create_directories(fs::path(full).parent_path(), ec);
  std::ofstream o(full, std::ios::binary | std::ios::trunc);
  if (!o) return false;
  o << data;
  return static_cast<bool>(o);
}

bool DirStorage::listDirs(const char* path, std::vector<std::string>& out) {
  std::string p = path ? path : "";
  if (p.find("..") != std::string::npos) return false;
  bool any = false;
  // Union of the read-only tree and the write root (uploads land there).
  std::vector<std::string> dirs = {root_ + p, writeRoot_.empty() ? "" : writeRoot_ + p};
  if (p == "/packs")
    for (const std::string& d : packDirs_) dirs.push_back(d);
  for (const std::string& dir : dirs) {
    if (dir.empty()) continue;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    any = true;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      std::string name = e.path().filename().string();
      if (e.is_directory() && std::find(out.begin(), out.end(), name) == out.end())
        out.push_back(name);
    }
  }
  return any;
}

bool FileCalendar::poll(std::vector<CalendarEvent>& out) {
  uint32_t ms = clock_->millis();
  if (!first_ && !force_ && ms - last_ < interval_) return false;
  int64_t now = clock_->epoch();
  if (now <= 0) return false;
  if (first_) base_ = now;
  first_ = force_ = false;
  last_ = ms;

  std::vector<CalendarEvent> evs;
  std::ifstream in(path_);
  if (in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string json = ss.str();
    // Translate relative "startIn"/"endIn" (seconds after simulator start)
    // into absolute times, then reuse the device parser.
    JsonDocument doc;
    if (!deserializeJson(doc, json)) {
      for (JsonObject o : doc["events"].as<JsonArray>()) {
        if (!o["startIn"].isNull()) o["start"] = base_ + o["startIn"].as<int64_t>();
        if (!o["endIn"].isNull()) o["end"] = base_ + o["endIn"].as<int64_t>();
      }
      std::string abs;
      serializeJson(doc, abs);
      parseCalendarJson(abs.data(), abs.size(), evs);
      Weather w;
      if (weather_ && parseWeatherJson(abs.data(), abs.size(), w) && weatherName(w) != lastWeather_) {
        lastWeather_ = weatherName(w);
        weather_->set(w);
      }
    }
  }
  evs.insert(evs.end(), extra_.begin(), extra_.end());
  std::sort(evs.begin(), evs.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.start < b.start; });
  out = evs;
  return true;
}

}  // namespace pc
}  // namespace dp
