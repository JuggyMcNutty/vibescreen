#ifndef __K_UTILS_H__
#define __K_UTILS_H__

#include "hv/json.hpp"
#include <vector>
#include <map>
#include <functional>
#include <algorithm>
#include <utility>

using json = nlohmann::json;

namespace KUtils {
  bool is_homed();

  // Whether the printer defines a gcode macro of this name. Nothing checks
  // whether Klipper accepted a command, so anything we send that not every
  // printer has must be tested for before it goes out rather than after.
  bool has_gcode_macro(const std::string &name);

  // Whether the printer's config carries this section, named as it is in
  // printer.cfg. Section names with an argument are spelled the same way
  // Klipper does, "gcode_shell_command guppy_input_shaper".
  //
  // Not answerable from the same place as has_gcode_macro.
  // printer.objects.list only reports objects that implement get_status, so
  // resonance_tester, adxl345 and calibrate_shaper_config are all absent from
  // it on a printer that plainly has them, measured on the K1 Max. configfile
  // is where a section shows up whether or not it reports status.
  bool has_config_section(const std::string &name);

  bool is_running_local();
  std::string get_root_path(const std::string root_name);

  // path, width
  std::pair<std::string, size_t> get_thumbnail(const std::string &gcode_file, json &j, double scale);

  std::string download_file(const std::string &root,
			    const std::string &fname,
			    const std::string &dest);

  std::vector<std::string> get_interfaces();
  std::string interface_ip(const std::string &interface);
  std::string get_wifi_interface();

  template <typename Out>
  void split(const std::string &s, char delim, Out result);

  std::vector<std::string> split(const std::string &s, char delim);

  std::string get_obj_name(const std::string &id);
  std::string to_title(std::string s);
  std::string eta_string(int64_t s);
  size_t bytes_to_mb(size_t s);

  // Numeric parsing that returns a fallback instead of throwing.
  //
  // std::stoi and std::stod throw on empty or non-numeric input, and there is
  // no try/catch anywhere in this codebase, so an unguarded parse of anything
  // user or config supplied takes the whole process down. Use these wherever
  // the input is not already known to be a well formed number.
  //
  // Note these also reject trailing garbage, which plain std::stoi accepts:
  // std::stoi("0.5") silently returns 0, which turned a fractional extrude
  // speed into a rejected "F0" move.
  int parse_int(const std::string &s, int fallback);
  double parse_double(const std::string &s, double fallback);

  // Hex colour values out of theme files and the Spoolman API.
  uint32_t parse_hex(const std::string &s, uint32_t fallback);

  template<typename T, typename U> void sort_map_values(std::map<T, U> v,
							std::vector<U> &out_vect,
							std::function<bool(U&, U&)> sorter) {
    for (auto &el : v) {
      out_vect.push_back(el.second);
    }

    std::sort(out_vect.begin(), out_vect.end(), sorter);
  };

  std::map<std::string, std::map<std::string, std::string>> parse_macros(json &m);

};

#endif // __K_UTILS_H__
