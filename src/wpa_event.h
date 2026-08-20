#ifndef __WPA_EVENT_H__
#define __WPA_EVENT_H__

#include "hv/hloop.h"
#include "hv/EventLoop.h"
#include "hv/EventLoopThread.h"

#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <functional>

class WpaEvent : private hv::EventLoopThread {
 public:
  WpaEvent();
  ~WpaEvent();

  void start();
  void stop();

  void register_callback(const std::string &name, std::function<void(const std::string&)>);
  void init_wpa();
  void handle_wpa_events(void *data, int len);

  // Runs the registered callbacks for everything the monitor socket has said
  // since the last call.
  //
  // Call this from the LVGL thread and nowhere else. The callbacks build table
  // rows and talk back to the control socket, and neither is safe from this
  // class's own event loop thread, which is where they used to run. See
  // docs/audit.md C19.
  void drain();

  // Also LVGL thread only, which is what drain() is for. wpa_ctrl_request
  // writes a command and reads the reply off the same datagram socket, so two
  // overlapping calls take each other's answers.
  std::string send_command(const std::string &cmd);

  static void _handle_wpa_events(hio_t *io, void *data, int readbyte) {
    WpaEvent* wpa_event = (WpaEvent*)hio_context(io);
    wpa_event->handle_wpa_events(data, readbyte);
  }

 private:
  struct wpa_ctrl *conn;
  std::map<std::string, std::function<void(const std::string&)>> callbacks;

  // Filled on this class's event loop thread, emptied on the LVGL thread.
  std::mutex queue_lock;
  std::deque<std::string> queue;
  static constexpr size_t queue_max = 128;
  bool queue_dropped = false;
};

#endif // __WPA_EVENT_H__
