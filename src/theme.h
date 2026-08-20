#ifndef __K_THEME_H__
#define __K_THEME_H__

#include "hv/json.hpp"

#include <string>

using json = nlohmann::json;

class ThemeConfig {
private:
  static ThemeConfig *instance;
  std::string path;

protected:
  json data;

public:
  ThemeConfig();
  ThemeConfig(ThemeConfig &o) = delete;
  void operator=(const ThemeConfig &) = delete;
  void init(const std::string config_path);

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

  static ThemeConfig *get_instance();

};

#endif // __K_THEME_H__
