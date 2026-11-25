#pragma once

#include <BoardConfig.h>

// Wi-Fi at Home: while the Home screen is showing, the reader joins a saved network in the
// background and stays connected, so network features have a connection ready. Opening a
// book, sleeping, turning the setting off, or 10 minutes on Home without a button press
// takes the radio down again.
//
// Only on boards with PSRAM: the X3 and X4 (ESP32-C3) keep Wi-Fi strictly on demand, as the
// radio's driver buffers would share their internal heap with Home's covers.
#define CROSSINK_APP_HOME_WIFI (!FREEINK_MCU_C3)

namespace HomeWifi {

// Main-loop driver; cheap when there is nothing to do. `onHome` is true while the Home
// screen is the current activity, `idleMs` the time since the last button press.
void tick(bool onHome, unsigned long idleMs);

// Called just before a new activity's onEnter(), so a join in flight is abandoned before
// that activity touches the radio. A book leaves the radio to be taken down on the next
// tick, after the reader's own onEnter().
void onActivityEntering(bool isHome, bool isReader);

// Takes the radio down now if Home brought it up (sleep, setting turned off).
void stop(const char* reason);

// True while a connection made at Home is up.
bool isConnected();

// True while the radio is joined to a network, whoever brought it up (every board). The
// status bar outside the reader shows a Wi-Fi icon then.
bool linkUp();

}  // namespace HomeWifi
