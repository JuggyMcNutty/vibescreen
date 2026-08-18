#include "utils.h"

#include "hv/requests.h"
#include "hv/hurl.h"
#include "config.h"
#include "state.h"
#include "spdlog/spdlog.h"

#include <cmath>
#include <time.h>
#include <sstream>
#include <iomanip>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <experimental/filesystem>
#include <regex>

namespace fs = std::experimental::filesystem;

namespace KUtils {

  bool is_homed() {
    auto v = State::get_instance()
      ->get_data("/printer_state/toolhead/homed_axes"_json_pointer);
    if (!v.is_null()) {
      std::string homed_axes = v.template get<std::string>();
      return homed_axes.find("x") != std::string::npos
	&& homed_axes.find("y") != std::string::npos
	&& homed_axes.find("z") != std::string::npos;
    }
    return false;
  }

  bool has_gcode_macro(const std::string &name) {
    auto &objects = State::get_instance()
      ->get_data("/printer_objs/objects"_json_pointer);
    if (objects.is_null()) {
      return false;
    }

    // printer.objects.list reports macros with whatever case the config used,
    // while configfile.settings lowercases them, so neither side can be
    // trusted to match a literal.
    const std::string want = "gcode_macro " + name;
    for (auto &o : objects) {
      const std::string obj_name = o.template get<std::string>();
      if (obj_name.size() == want.size()
	  && std::equal(obj_name.begin(), obj_name.end(), want.begin(),
			[](char a, char b) {
			  return std::tolower(static_cast<unsigned char>(a))
			    == std::tolower(static_cast<unsigned char>(b));
			})) {
	return true;
      }
    }
    return false;
  }

  bool has_config_section(const std::string &name) {
    auto &settings = State::get_instance()
      ->get_data("/printer_state/configfile/settings"_json_pointer);
    if (!settings.is_object()) {
      return false;
    }

    // Klipper lowercases section names in configfile.settings, so a caller can
    // pass the name with whatever case printer.cfg used and still match.
    std::string want = name;
    std::transform(want.begin(), want.end(), want.begin(),
		   [](unsigned char c) { return std::tolower(c); });

    return settings.contains(want);
  }

  double config_number(const std::string &section,
		       const std::string &key,
		       double fallback) {
    auto &settings = State::get_instance()
      ->get_data("/printer_state/configfile/settings"_json_pointer);
    if (!settings.is_object()) {
      return fallback;
    }

    std::string want = section;
    std::transform(want.begin(), want.end(), want.begin(),
		   [](unsigned char c) { return std::tolower(c); });

    if (!settings.contains(want)) {
      return fallback;
    }

    auto &v = settings[want];
    if (!v.is_object() || !v.contains(key)) {
      return fallback;
    }

    // Klipper types these properly in settings, unlike the raw config text it
    // also serves, but a gcode_macro variable can still arrive as a string.
    auto &n = v[key];
    if (n.is_number()) {
      return n.template get<double>();
    }
    if (n.is_string()) {
      return parse_double(n.template get<std::string>(), fallback);
    }
    return fallback;
  }

  // The value below which an [output_pin] fan does not turn at all.
  //
  // Creality keeps these in the PRINTER_PARAM macro's variables, one per fan,
  // named after the pin: fan2_min for output_pin fan2. Measured on a K1 Max
  // they are 25, 50 and 180 out of 255 for the toolhead, back and side fans.
  // Their own M106 maps a requested speed onto min..255 so that the whole
  // control does something.
  //
  // Zero on anything that does not have the macro, which makes the mapping
  // below the identity and is what a non-Creality machine wants.
  static double fan_min_raw(const std::string &fan_id) {
    auto &v = State::get_instance()->get_data(json::json_pointer(
	fmt::format("/printer_state/gcode_macro PRINTER_PARAM/{}_min",
		    get_obj_name(fan_id))));
    if (v.is_number()) {
      return v.template get<double>();
    }
    if (v.is_string()) {
      return parse_double(v.template get<std::string>(), 0.0);
    }
    return 0.0;
  }

  // What VALUE is measured in. 255 on a K1, 1 by default in Klipper.
  //
  // Falls back to 255 rather than to Klipper's own default, because that is
  // what this code sent unconditionally before it asked, and the only way to
  // reach the fallback is the config not being readable at all.
  static double fan_scale(const std::string &fan_id) {
    double scale = config_number(fan_id, "scale", 255.0);
    return scale > 0.0 ? scale : 255.0;
  }

  double fan_pct_to_raw(const std::string &fan_id, int pct) {
    if (pct <= 0) {
      // Off is off. Creality's M106 does the same: it only applies the minimum
      // when a non-zero speed was asked for, so that S0 really stops the fan
      // rather than parking it at its slowest turning speed.
      return 0.0;
    }

    double scale = fan_scale(fan_id);
    double min = fan_min_raw(fan_id);
    if (min <= 0.0 || min >= scale) {
      return scale * pct / 100.0;
    }

    return min + (scale - min) * pct / 100.0;
  }

  int fan_value_to_pct(const std::string &fan_id, double value) {
    double scale = fan_scale(fan_id);
    double raw = value * scale;
    double min = fan_min_raw(fan_id);

    double pct = (min <= 0.0 || min >= scale)
      ? raw * 100.0 / scale
      : (raw - min) * 100.0 / (scale - min);

    // A pin sitting just under the minimum is a fan that is not turning, and
    // the arithmetic gives a small negative for it.
    if (pct < 0.0) {
      return 0;
    }
    return pct > 100.0 ? 100 : static_cast<int>(pct + 0.5);
  }

  bool is_running_local() {
    Config *conf = Config::get_instance();
    std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");
    return df_host == "localhost" || df_host == "127.0.0.1";
  }

  std::string get_root_path(const std::string root_name) {
    auto roots = State::get_instance()->get_data("/roots"_json_pointer);
    json filtered;
    std::copy_if(roots.begin(), roots.end(),
		 std::back_inserter(filtered), [&root_name](const json& item) {
		   return item.contains("name") && item["name"] == root_name;
		 });

    spdlog::trace("roots {}, filtered {}", roots.dump(), filtered.dump());
    if (!filtered.empty()) {
      return filtered["/0/path"_json_pointer];
    }

    return "";
  }

  std::pair<std::string, size_t> get_thumbnail(const std::string &gcode_file, json &j, double scale) {
    auto &thumbs = j["/result/thumbnails"_json_pointer];
    if (!thumbs.is_null() && !thumbs.empty()) {
      // assume square, look for closest to 300x300
      auto scaled_width = scale * 300;
      spdlog::debug("using thumb at scaled width {}", scaled_width);
      uint32_t closest_index = 0;
      size_t thumb_width = 0;
      auto width = thumbs.at(0)["width"].is_number()
	? thumbs.at(0)["width"].template get<int>()
	: std::stoi(thumbs.at(0)["width"].template get<std::string>());
      int closest = std::abs(scaled_width - width);
      for (int i = 0; i < thumbs.size(); i++) {
	width = thumbs.at(i)["width"].is_number()
	  ? thumbs.at(i)["width"].template get<int>()
	  : std::stoi(thumbs.at(i)["width"].template get<std::string>());
	int cur_diff = std::abs(scaled_width - width);
	if (cur_diff < closest) {
	  closest = cur_diff;
	  closest_index = i;
	  thumb_width = width;
	}
      }

      auto &thumb = thumbs.at(closest_index);
      spdlog::debug("using thumb at index {}, {}", closest_index, thumbs.dump());

      // metadata thumbnail paths are relative to the current gcode file directory
      std::string relative_path = thumb["relative_path"].template get<std::string>();
      size_t found = gcode_file.find_last_of("/\\");
      if (found != std::string::npos) {
	relative_path = gcode_file.substr(0, found + 1) + relative_path;
      }

      Config *conf = Config::get_instance();
      std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");
      std::string fname = relative_path.substr(relative_path.find_last_of("/\\") + 1);
      std::string fullpath = fmt::format("{}/{}", conf->get<std::string>("/thumbnail_path"), fname);
    
      // download thumbnail
      if (is_running_local()) {
	spdlog::debug("running locally, skipping thumbnail downloads");
	auto gcode_root = get_root_path("gcodes");
	fullpath = fmt::format("{}/{}", gcode_root, relative_path);
      } else {
	std::string thumb_url = fmt::format("http://{}:{}/server/files/gcodes/{}",
					    df_host,
					    conf->get<uint32_t>(conf->df() + "moonraker_port"),
					    HUrl::escape(relative_path));


	// threadpool this
	spdlog::debug("thumb url {}", thumb_url);
	auto size = requests::downloadFile(thumb_url.c_str(), fullpath.c_str());
	spdlog::trace("downloaded size {}", size);
      }

      return std::make_pair(fullpath, thumb_width);
    }

    return std::make_pair("", 0);
  }

  std::string download_file(const std::string &root,
			    const std::string &fname,
			    const std::string &dest) {

    auto filename = fs::path(fname).filename();
    auto dest_fullpath = fs::path(dest) / filename;

    spdlog::trace("root {}, fname {}, base filename {}, dest_fp {}", root, fname,
		  filename.string(), dest_fullpath.string());
    Config *conf = Config::get_instance();
    std::string df_host = conf->get<std::string>(conf->df() + "moonraker_host");

    std::string file_url = fmt::format("http://{}:{}/server/files/{}/{}",
					df_host,
					conf->get<uint32_t>(conf->df() + "moonraker_port"),
					root,
					HUrl::escape(fname));
    // threadpool this
    spdlog::debug("file url {}", file_url);
    auto size = requests::downloadFile(file_url.c_str(), dest_fullpath.c_str());
    spdlog::trace("downloaded file size {}", size);

    return dest_fullpath.string();
  }

  std::vector<std::string> get_interfaces() {
    std::vector<std::string> ifaces;
    struct ifaddrs *addrs;
    getifaddrs(&addrs);
    for (struct ifaddrs *addr = addrs; addr != nullptr; addr = addr->ifa_next) {
        if (addr->ifa_addr && addr->ifa_addr->sa_family == AF_PACKET) {
	  ifaces.push_back(addr->ifa_name);
        }
    }

    freeifaddrs(addrs);
    return ifaces;
  }

  std::string interface_ip(const std::string &interface) {
    // The name comes from get_wifi_interface below, which reads it out of the
    // configurable wpa_supplicant directory, so it is not a fixed string and
    // must be bounded against IFNAMSIZ.
    if (interface.empty() || interface.size() >= IFNAMSIZ) {
      spdlog::warn("interface name '{}' is empty or too long", interface);
      return "";
    }

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (fd < 0) {
      spdlog::warn("cannot open socket to query {}: {}", interface, strerror(errno));
      return "";
    }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ - 1);

    // Without this check a failed ioctl leaves ifr_addr as the zero from the
    // initialiser and the function confidently reports 0.0.0.0.
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
      spdlog::debug("no address for {}: {}", interface, strerror(errno));
      close(fd);
      return "";
    }
    close(fd);

    const char *ip = inet_ntoa(((sockaddr_in *) &ifr.ifr_addr)->sin_addr);
    return ip != NULL ? std::string(ip) : std::string();
  }

  std::string get_wifi_interface() {
    std::string wpa_socket = Config::get_instance()->get<std::string>("/wpa_supplicant");
    if (fs::is_directory(fs::status(wpa_socket))) {
      for (const auto &e : fs::directory_iterator(wpa_socket)) {
        if (fs::is_socket(e.path()) && e.path().string().find("p2p") == std::string::npos) {
          return e.path().filename().string();
        }
      }
    }

    return "";
  }

  template <typename Out>
  void split(const std::string &s, char delim, Out result) {
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, delim)) {
        *result++ = item;
    }
  }

  std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
  }

  std::string get_obj_name(const std::string &id) {
    size_t pos = id.find_last_of(' ');
    return id.substr(pos + 1);
  }

  std::string to_title(std::string s) {
    bool last = true;
    for (char& c : s) {
      c = last ? std::toupper(c) : std::tolower(c);
      if (c == '_') {
	c = ' ';
      }

      last = std::isspace(c);
    }
    return s;
  }


  std::string eta_string(int64_t s) {
    time_t seconds (s);
    tm p;
    gmtime_r (&seconds, &p);

    std::ostringstream os;

    if (p.tm_yday > 0)
      os << p.tm_yday << "d ";

    if (p.tm_hour > 0)
      os << p.tm_hour << "h ";

    if (p.tm_min > 0)
      os << p.tm_min << "m ";

    os << p.tm_sec << "s";
    
    return os.str();
  }

  size_t bytes_to_mb(size_t s) {
    return s / 1024 / 1024;
  }

  std::map<std::string, std::map<std::string, std::string>> parse_macros(json &m) {
    std::map<std::string, std::map<std::string, std::string>> macros;

    std::regex param_regex(R"(params\.(\w+)(.*))", std::regex_constants::icase);
    std::regex default_value_regex(R"(\|\s*default\s*\(\s*((["'])(?:\\.|[^\x02])*\2|-?[0-9][^,)]*))",
                                   std::regex_constants::icase);
    for (auto &el : m.items()) {
      std::string key = el.key();
      if (key.rfind("gcode_macro ", 0) == 0) {
        auto &gcode = el.value()["/gcode"_json_pointer];
        if (!gcode.is_null()) {
          auto macro_split = split(el.key(), ' ');
          if (macro_split.size() > 1 && macro_split[1].rfind("_", 0) != 0) {
            std::string macro_name = macro_split[1];

            const auto &gcode_str = gcode.template get<std::string>();
            auto param_begin =
                std::sregex_iterator(gcode_str.begin(), gcode_str.end(), param_regex);
            auto param_end = std::sregex_iterator();

            std::map<std::string, std::string> macro_params;
            for (std::sregex_iterator i = param_begin; i != param_end; ++i) {
              std::smatch match = *i;
              std::string param_name = match.str(1);
              std::string rest = match.str(2);
              std::smatch matches;
              std::string default_value = "";

              spdlog::trace("macro: {}, param; {}, rest: {}", macro_name, param_name, rest);

              if (std::regex_search(rest, matches, default_value_regex)) {
                default_value = matches.str(1);
              }

              macro_params.insert({param_name, default_value});
            }
            macros.insert({macro_name, macro_params});
          }
        }
      }
    }

    return macros;
  }

  int parse_int(const std::string &s, int fallback) {
    try {
      size_t consumed = 0;
      int v = std::stoi(s, &consumed);
      // Reject trailing junk. std::stoi("0.5") happily returns 0.
      if (consumed != s.size()) {
        spdlog::warn("parse_int: trailing characters in '{}', using {}", s, fallback);
        return fallback;
      }
      return v;
    } catch (const std::exception &e) {
      spdlog::warn("parse_int: cannot parse '{}' ({}), using {}", s, e.what(), fallback);
      return fallback;
    }
  }

  uint32_t parse_hex(const std::string &s, uint32_t fallback) {
    try {
      size_t consumed = 0;
      unsigned long v = std::stoul(s, &consumed, 16);
      if (consumed != s.size()) {
        spdlog::warn("parse_hex: trailing characters in '{}', using {:#x}", s, fallback);
        return fallback;
      }
      return (uint32_t)v;
    } catch (const std::exception &e) {
      spdlog::warn("parse_hex: cannot parse '{}' ({}), using {:#x}", s, e.what(), fallback);
      return fallback;
    }
  }

  double parse_double(const std::string &s, double fallback) {
    try {
      size_t consumed = 0;
      double v = std::stod(s, &consumed);
      if (consumed != s.size()) {
        spdlog::warn("parse_double: trailing characters in '{}', using {}", s, fallback);
        return fallback;
      }
      return v;
    } catch (const std::exception &e) {
      spdlog::warn("parse_double: cannot parse '{}' ({}), using {}", s, e.what(), fallback);
      return fallback;
    }
  }
  }  // namespace KUtils
