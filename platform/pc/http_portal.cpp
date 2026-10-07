#include "http_portal.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "deskpet/pack.h"
#include "deskpet/portal.h"

namespace dp {
namespace pc {

namespace {

std::string response(int code, const char* type, const std::string& body) {
  const char* text = code == 200 ? "OK" : code == 404 ? "Not Found" : "Bad Request";
  char head[160];
  std::snprintf(head, sizeof(head),
                "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                code, text, type, body.size());
  return head + body;
}

std::string queryArg(const std::string& target, const std::string& key) {
  size_t q = target.find('?');
  if (q == std::string::npos) return "";
  std::string qs = target.substr(q + 1);
  size_t pos = 0;
  while (pos <= qs.size()) {
    size_t amp = qs.find('&', pos);
    std::string kv = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
    size_t eq = kv.find('=');
    if (eq != std::string::npos && kv.substr(0, eq) == key) {
      std::string v, raw = kv.substr(eq + 1);
      for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '%' && i + 2 < raw.size()) {
          v += static_cast<char>(std::strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16));
          i += 2;
        } else {
          v += raw[i] == '+' ? ' ' : raw[i];
        }
      }
      return v;
    }
    if (amp == std::string::npos) break;
    pos = amp + 1;
  }
  return "";
}

std::string header(const std::string& head, const std::string& name) {
  std::string lower = head;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  std::string key = "\r\n" + name + ":";
  size_t p = lower.find(key);
  if (p == std::string::npos) return "";
  size_t s = p + key.size();
  size_t e = head.find("\r\n", s);
  std::string v = head.substr(s, e - s);
  while (!v.empty() && v[0] == ' ') v.erase(0, 1);
  return v;
}

}  // namespace

void HttpPortal::start(PortalHost& host) {
  host_ = &host;
  if (fd_ >= 0) return;
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(fd_, 4) != 0) {
    std::perror("portal: bind");
    close(fd_);
    fd_ = -1;
    return;
  }
  fcntl(fd_, F_SETFL, O_NONBLOCK);
  std::printf("portal: room editor at http://localhost:%d\n", port_);
}

void HttpPortal::stop() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  host_ = nullptr;
}

std::vector<std::string> HttpPortal::statusLines() {
  if (fd_ < 0) return {"Portal failed", "port in use?"};
  std::vector<std::string> l = {"Open in browser", "localhost:" + std::to_string(port_)};
  if (!last_.empty()) l.push_back(last_);
  return l;
}

void HttpPortal::loop() {
  if (fd_ < 0) return;
  int c = accept(fd_, nullptr, nullptr);
  if (c < 0) return;
  // Requests are small; read them fully (blocking, with a timeout).
  timeval tv{2, 0};
  setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  std::string req;
  char buf[4096];
  size_t want = 0;
  for (;;) {
    ssize_t n = recv(c, buf, sizeof(buf), 0);
    if (n <= 0) break;
    req.append(buf, static_cast<size_t>(n));
    size_t he = req.find("\r\n\r\n");
    if (he != std::string::npos && want == 0) {
      want = he + 4 + static_cast<size_t>(std::atol(header(req.substr(0, he + 2), "content-length").c_str()));
    }
    if (want && req.size() >= want) break;
    if (req.size() > 256 * 1024) break;
  }
  std::string resp = handle(req);
  send(c, resp.data(), resp.size(), 0);
  close(c);
}

std::string HttpPortal::handle(const std::string& req) {
  size_t he = req.find("\r\n\r\n");
  if (he == std::string::npos || !host_) return response(400, "text/plain", "bad request");
  std::string head = req.substr(0, he + 2);
  std::string body = req.substr(he + 4);
  std::string method = head.substr(0, head.find(' '));
  size_t t0 = head.find(' ') + 1;
  std::string target = head.substr(t0, head.find(' ', t0) - t0);
  std::string path = target.substr(0, target.find('?'));

  if (method == "GET" && path == "/") return response(200, "text/html", kPortalPage);
  if (method == "GET" && path == "/room") return response(200, "application/json", host_->roomJson());
  if (method == "GET" && path == "/rooms") return response(200, "application/json", roomListJson(*host_));
  if (method == "POST" && path == "/room") {
    std::string err;
    if (!host_->applyRoomJson(body, err)) return response(400, "text/plain", err);
    last_ = "Room edited";
    return response(200, "text/plain", "ok");
  }
  if (method == "POST" && path == "/room/select") {
    if (!host_->selectRoom(queryArg(target, "id"))) return response(400, "text/plain", "cannot load room");
    return response(200, "text/plain", "ok");
  }
  if (method == "POST" && path == "/upload") {
    // Minimal multipart/form-data: one file per request (the page sends
    // files one by one).
    std::string ct = header(head, "content-type");
    size_t b = ct.find("boundary=");
    if (b == std::string::npos) return response(400, "text/plain", "expected multipart");
    std::string boundary = "--" + ct.substr(b + 9);
    size_t fn = body.find("filename=\"");
    if (fn == std::string::npos) return response(400, "text/plain", "no file");
    size_t fe = body.find('"', fn + 10);
    std::string name = body.substr(fn + 10, fe - fn - 10);
    size_t ds = body.find("\r\n\r\n", fe);
    size_t de = body.find("\r\n" + boundary, ds);
    if (ds == std::string::npos || de == std::string::npos) return response(400, "text/plain", "bad multipart");
    std::string data = body.substr(ds + 4, de - ds - 4);
    std::string dest, err;
    if (!portalUploadPath(queryArg(target, "kind"), queryArg(target, "id"), name, dest, err))
      return response(400, "text/plain", err);
    if (data.size() > kMaxSpriteBytes) return response(400, "text/plain", "file too large");
    if (!storage_.write(dest.c_str(), data)) return response(400, "text/plain", "cannot write");
    last_ = "Got " + name;
    return response(200, "text/plain", "ok");
  }
  return response(404, "text/plain", "not found");
}

}  // namespace pc
}  // namespace dp
