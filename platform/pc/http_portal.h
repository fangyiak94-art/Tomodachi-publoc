// The portal for PC targets: a tiny HTTP server on localhost serving the
// shared room editor page. Single-threaded and polled from Engine::loop(),
// like the board's WebServer.
#pragma once
#include <string>

#include "deskpet/hal.h"

namespace dp {
namespace pc {

class HttpPortal : public PackUploader {
 public:
  HttpPortal(Storage& storage, int port) : storage_(storage), port_(port) {}
  ~HttpPortal() override { stop(); }
  void start(PortalHost& host) override;
  void stop() override;
  void loop() override;
  std::vector<std::string> statusLines() override;

  // Handles one raw request (used by loop() and by tests without sockets).
  std::string handle(const std::string& request);

 private:
  Storage& storage_;
  PortalHost* host_ = nullptr;
  int port_;
  int fd_ = -1;
  std::string last_;
};

}  // namespace pc
}  // namespace dp
