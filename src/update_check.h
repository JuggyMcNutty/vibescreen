#ifndef __UPDATE_CHECK_H__
#define __UPDATE_CHECK_H__

#include <functional>
#include <string>

// Polls GitHub for a newer release and reports whether one exists.
//
// The check runs `update.sh --check`, which prints key=value and installs
// nothing, rather than talking to GitHub from here. libhv is built with
// WITH_OPENSSL=no so this binary has no TLS at all, and giving it a TLS stack
// and a trust store to fetch one version string would be a poor trade when the
// same check already lives in the updater and python3 is already required by
// it. It also keeps one implementation of what counts as newer, instead of two
// that drift.
//
// Governed by two config keys, both global:
//
//   update_check_enabled          bool, default true
//   update_check_interval_hours   int, default 24, minimum 1
class UpdateCheck {
 public:
  // Starts polling if enabled. Safe to call when there is no update.sh, which
  // is the case in the simulator and on the Debian packaging: it simply never
  // reports anything. LVGL thread.
  static void start();

  // Re-reads both config keys and restarts the timer. Call after changing
  // either. Turning the check off also clears any notice already showing, so
  // the setting takes effect immediately rather than at the next poll. LVGL
  // thread.
  static void reconfigure();

  // Runs a check now, regardless of the interval, unless one is in flight.
  // LVGL thread.
  static void check_now();

  static bool update_available();

  // The tag of the release on offer, empty unless update_available().
  static std::string latest_version();

  // Called on the LVGL thread whenever update_available() changes. One
  // listener, which is MainPanel showing or hiding its badge.
  static void set_listener(std::function<void()> cb);
};

#endif // __UPDATE_CHECK_H__
