#ifndef __K_CONFIG_H__
#define __K_CONFIG_H__

#include "hv/json.hpp"
#include "spdlog/spdlog.h"

#include <string>

using json = nlohmann::json;

class Config {
 private:
  static Config *instance;
  std::string path;

 protected:
  json data;
  std::string default_printer;

 public:
  Config();
  Config(Config &o) = delete;
  void operator=(const Config &) = delete;
  void init(std::string config_path, const std::string thumbdir);

  // Reads must not insert. operator[] on a json_pointer default-inserts a null
  // for a missing key, so a read turned into a write and the next save() put
  // that null in the file. Falling back to a null json rather than throwing
  // keeps the old behaviour for every caller: get<json> on a missing key still
  // yields null, and every other T still throws on it.
  template<typename T> T get(const std::string &json_ptr) {
    static const json absent;
    auto ptr = json::json_pointer(json_ptr);
    return data.contains(ptr) ? data.at(ptr).template get<T>() : absent.template get<T>();
  };

  template<typename T> T set(const std::string &json_ptr, T v) {
    return data[json::json_pointer(json_ptr)] = v;
  };

  const json &get_json(const std::string &json_path);

  void save();
  std::string& df();
  std::string get_thumbnail_path();
  std::string get_wifi_interface();
  std::string get_path();

  static Config *get_instance();

};

#endif // __K_CONFIG_H__
