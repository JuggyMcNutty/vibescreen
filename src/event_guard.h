#ifndef __EVENT_GUARD_H__
#define __EVENT_GUARD_H__

#include "spdlog/spdlog.h"

#include <exception>

// Contains exceptions inside LVGL event callbacks.
//
// LVGL is C and calls our callbacks from inside its own frames. An exception
// must never be allowed to unwind back through them, because LVGL cannot be
// recovered once it has. Both of the obvious alternatives were built and
// measured before settling on this:
//
//   - Catching in the main loop around lv_timer_handler leaves that function's
//     re-entrancy guard set, since the line that clears it is skipped. Every
//     later call then returns immediately and the UI freezes while the process
//     stays alive, which is worse than crashing because the init script
//     restarts a dead process but never a wedged one.
//
//   - Patching LVGL to release that guard from the catch does release it, but
//     the input state machine is still interrupted mid gesture, so it
//     re-dispatches the same event forever. One injected throw produced 215
//     exceptions and no further input was accepted.
//
// Catching here, before anything unwinds out of the trampoline, leaves LVGL
// untouched. Panels stay free to throw, and a throw costs one log line and one
// ignored interaction instead of the whole UI.
namespace KGuard {
  template <typename F>
  inline void event(const char *where, F &&fn) {
    try {
      fn();
    } catch (const std::exception &e) {
      spdlog::error("exception in {}, {}", where, e.what());
    } catch (...) {
      spdlog::error("unknown exception in {}", where);
    }
  }
}

#endif // __EVENT_GUARD_H__
