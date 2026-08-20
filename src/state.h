#ifndef __STATE_H__
#define __STATE_H__

#include <mutex>
#include <vector>
#include "notify_consumer.h"

class State : public NotifyConsumer {
 private:
  static State *instance;
  static std::mutex lock;
  
 protected:
  json data;

 public:
  State(std::mutex &lv_lock);
  State(State &o) = delete;
  void operator=(const State &) = delete;

  void reset();
  void set_data(const std::string &key, json &j, const std::string &json_path);
  // Const because callers hold the reference after the lock is released. That
  // was the use-after-free in docs/audit.md C1 while dispatch ran on libhv's
  // thread; now that every caller and set_data itself are on the LVGL thread
  // the reference is stable, and const keeps it a read.
  const json &get_data();
  const json &get_data(const json::json_pointer &ptr);

  void consume(json &j);

  std::vector<std::string> get_extruders();
  std::vector<std::string> get_heaters();
  std::vector<std::string> get_sensors();
  std::vector<std::string> get_fans();
  std::vector<std::string> get_leds();
  std::vector<std::string> get_output_pins();

  json get_display_sensors();
  json get_display_fans();
  json get_display_leds();

  static State *get_instance();
};

#endif // __STATE_H__
