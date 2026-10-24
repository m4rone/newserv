#pragma once

#include <stdint.h>

// Spire guard (v33): the lobby restore after a Spire game waits until the client has loaded the lobby. Measured
// 2026-10-24 15:56 (lab) and 18:39 (pc): the B2 of the restore went out in the same second as the 67 that put the
// client in the lobby, and both clients hung on the lobby join (alive, empty lobby, no own character). The client
// sends nothing of its own when the lobby is loaded, but it answers a B1 (server time) with a 99 only after loading
// (newserv uses the same trick for Episode 3 spectators, see on_99). So the server sends a B1 right after the 67 and
// starts the restore (30, B2, 61) at the 99, or after SPIRE_GUARD_LOBBY_SETTLE_MS without one. This gate is the
// decision, kept free of asio for the static tests: arm() when the 67 and the B1 went out; the first on_lobby_loaded()
// (the 99) or on_timeout() after that returns true (the restore starts), every later event returns false.
constexpr uint32_t SPIRE_GUARD_LOBBY_SETTLE_MS = 3000;

struct SpireGuardLobbyGate {
  enum class Event { NONE, LOBBY_LOADED, TIMEOUT };

  bool armed = false;
  Event fired = Event::NONE;

  void arm() {
    this->armed = true;
    this->fired = Event::NONE;
  }
  bool on_lobby_loaded() {
    return this->fire(Event::LOBBY_LOADED);
  }
  bool on_timeout() {
    return this->fire(Event::TIMEOUT);
  }

private:
  bool fire(Event event) {
    if (!this->armed || (this->fired != Event::NONE)) {
      return false;
    }
    this->fired = event;
    return true;
  }
};
