#pragma once

#include "network/HomeWifi.h"

// The file transfer web page (http://<IP>/ and http://ashadsbook.local/, WebDAV, and the
// Calibre plugin's discovery) served in the background while Wi-Fi at Home is connected,
// with no File Transfer screen. HomeWifi starts and stops it with its connection; it runs
// only while Home is showing.
//
// Boards with PSRAM only, like Wi-Fi at Home.
namespace HomeWebServer {

// Starts the server on the connection that is up. Logged and skipped on failure.
void start();

// Stops it (a book opens, Home hands over to another screen, the radio goes down).
void stop();

// Serves the requests waiting. Main loop only; cheap when the server is not running.
void handle();

bool running();

// True while a browser is in the middle of sending a file: the device stays awake.
bool uploading();

}  // namespace HomeWebServer
