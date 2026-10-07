// The web page and request rules for the portal (room editor + uploads).
// Shared by every target so the simulator and the board serve the same
// page; targets only provide the HTTP transport.
//
// Routes:
//   GET  /                     the page
//   GET  /room                 active room as JSON
//   POST /room                 body = room JSON -> live edit (saved as "custom")
//   GET  /rooms                {"rooms":[...],"active":"id"}
//   POST /room/select?id=x     switch theme
//   POST /upload?kind=pack|room&id=x   multipart file upload
#pragma once
#include <string>

#include "deskpet/hal.h"

namespace dp {

extern const char kPortalPage[];

std::string roomListJson(PortalHost& host);

// Where an uploaded file goes, e.g. "/rooms/den/house.dps". Rejects unsafe
// ids and names. `kind` is "pack" or "room".
bool portalUploadPath(const std::string& kind, const std::string& id, const std::string& file,
                      std::string& path, std::string& err);

}  // namespace dp
