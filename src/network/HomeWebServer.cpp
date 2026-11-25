#include "HomeWebServer.h"

#if CROSSINK_APP_HOME_WIFI

#include <ESPmDNS.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <memory>

#include "network/CrossPointWebServer.h"

namespace {
// The name File Transfer announces too: http://crosspoint.local/
constexpr const char* HOSTNAME = "crosspoint";
// Requests served per main-loop pass, as File Transfer serves a batch per pass.
constexpr int REQUESTS_PER_PASS = 8;

// Held only while running: the server's buffers (two 4 KB upload buffers, the sockets)
// are not worth keeping on Home's heap while Wi-Fi is off.
std::unique_ptr<CrossPointWebServer> server;
bool mdnsStarted = false;
}  // namespace

void HomeWebServer::start() {
  if (server) return;
  server = makeUniqueNoThrow<CrossPointWebServer>();
  if (!server) {
    LOG_ERR("HWEB", "OOM: background web server (free=%u maxAlloc=%u)", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    return;
  }
  server->begin();
  if (!server->isRunning()) {
    LOG_ERR("HWEB", "Background web server did not start");
    server.reset();
    return;
  }
  mdnsStarted = MDNS.begin(HOSTNAME);
  if (!mdnsStarted) LOG_ERR("HWEB", "mDNS did not start; the page is still at the IP address");
  LOG_INF("HWEB", "Web page at http://%s/ and http://%s.local/ (free=%u)", WiFi.localIP().toString().c_str(), HOSTNAME,
          ESP.getFreeHeap());
}

void HomeWebServer::stop() {
  if (!server) return;
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  server->stop();
  server.reset();
  LOG_INF("HWEB", "Background web server stopped (free=%u)", ESP.getFreeHeap());
}

void HomeWebServer::handle() {
  if (!server || !server->isRunning()) return;
  for (int i = 0; i < REQUESTS_PER_PASS && server->isRunning(); i++) server->handleClient();
}

bool HomeWebServer::running() { return server && server->isRunning(); }

bool HomeWebServer::uploading() { return server && server->wsUploadActive(); }

#else

void HomeWebServer::start() {}
void HomeWebServer::stop() {}
void HomeWebServer::handle() {}
bool HomeWebServer::running() { return false; }
bool HomeWebServer::uploading() { return false; }

#endif
