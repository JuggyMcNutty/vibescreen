#include "config.h"

#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

Config *Config::instance{NULL};

Config::Config() {
}

Config *Config::get_instance() {
  if (instance == NULL) {
    instance = new Config();
  }
  return instance;
}

void Config::init(std::string config_path, const std::string thumbdir) {
  path = config_path;
  struct stat buffer;
  json fans_conf = {
    {
      {"id", "output_pin fan0"},
      {"display_name", "Toolhead Fan"}
    },
    {
      {"id", "output_pin fan1"},
      {"display_name", "Back Fan"}
    },
    {
      {"id", "output_pin fan2"},
      {"display_name", "Side Fan"}
    }
  };

  json sensors_conf = {
    {
      {"id", "extruder"},
      {"display_name", "Extruder"},
      {"controllable", true},
      {"color", "red"}
    },
    {
      {"id", "heater_bed"},
      {"display_name", "Bed"},
      {"controllable", true},
      {"color", "purple"}
    },
    {
      {"id", "temperature_sensor chamber_temp"},
      {"display_name", "Chamber"},
      {"controllable", false},
      {"color", "blue"}
    }
  };

  json cooldown_conf = {{ "cooldown", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0\nSET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0"}};
  json default_macros_conf = {
    {"load_filament", "_GUPPY_LOAD_MATERIAL"},
    {"unload_filament", "_GUPPY_QUIT_MATERIAL"}
  };

  // Spoolman is probed at every connect whenever Moonraker reports the
  // component, and there was no way to say no. A Spoolman version guppy cannot
  // parse, or one configured in Moonraker but not actually reachable, then
  // makes itself felt on every start with nothing to do about it. Upstream PR
  // #61 and issues #107 and #115.
  bool default_disable_spoolman = false;

  // Options offered on the extruder panel. Wide enough to cover exotic
  // filaments: PC and nylon blends want 260 to 300, PPS-CF and similar go
  // higher still, and composite purges need far more than the 35mm this used
  // to top out at.
  //
  // These are only what the UI offers. What the printer will actually accept
  // is read from the live Klipper config at runtime and anything out of range
  // is greyed out, so a generous list here is safe on a smaller hotend.
  //
  // The panel splits each list in half to make two rows, so an even count
  // gives an even layout.
  json extrude_temps_conf = {170, 190, 200, 210, 220, 230, 240, 250, 260, 280, 300, 320};
  json extrude_lengths_conf = {5, 10, 15, 20, 25, 30, 40, 50, 75, 100, 150, 200};
  json extrude_speeds_conf = {1, 2, 3, 5, 8, 10, 15, 20, 25, 30, 40, 50};

  // /usr/data only exists on the printer. A simulator build defaulted its log
  // there too, where it cannot be written, so put it beside the config instead.
#ifdef SIMULATOR
  std::string default_log_path =
    (fs::path(config_path).parent_path() / "guppyscreen.log").string();
#else
  std::string default_log_path = "/usr/data/printer_data/logs/guppyscreen.log";
#endif

  if (stat(config_path.c_str(), &buffer) == 0) {
    data = json::parse(std::fstream(config_path));
  } else {
    data = {
        {"log_path", default_log_path},
        {"thumbnail_path", thumbdir},
        {"wpa_supplicant", "/var/run/wpa_supplicant"},
        {"display_sleep_sec", 600},
        {"default_printer", "k1"},
        {"printers", {{"k1", {
                                 {"moonraker_api_key", false},
                                 {"moonraker_host", "127.0.0.1"},
                                 {"moonraker_port", 7125},
                                 {"monitored_sensors", sensors_conf},
                                 {"fans", fans_conf},
                                 {"default_macros", default_macros_conf},
                                 {"disable_spoolman", default_disable_spoolman},
                             }}}
        }
    };
  }

  data["config_path"] = config_path;

  auto df_name = data["/default_printer"_json_pointer];
  if (!df_name.is_null()) {
    default_printer = "/printers/" + df_name.template get<std::string>() + "/";

    auto &monitored_sensors = data[json::json_pointer(df() + "monitored_sensors")];
    if (monitored_sensors.is_null()) {
      data[json::json_pointer(df() + "monitored_sensors")] = sensors_conf;
    }

    auto &fans = data[json::json_pointer(df() + "fans")];
    if (fans.is_null()) {
      data[json::json_pointer(df() + "fans")] = fans_conf;
    }

    auto &default_macros = data[json::json_pointer(df() + "default_macros")];
    if (default_macros.is_null()) {
      default_macros_conf.merge_patch(cooldown_conf);
      data[json::json_pointer(df() + "default_macros")] = default_macros_conf;
    } else {
      if (!default_macros.contains("cooldown")) {
        default_macros.merge_patch(cooldown_conf);
      }
    }

    // Backfilled rather than only set in the defaults block above, because
    // that block is skipped entirely when a config file already exists. An
    // upgrade would otherwise land on empty extruder selectors.
    auto &extrude_temps = data[json::json_pointer(df() + "extrude_temps")];
    if (extrude_temps.is_null()) {
      data[json::json_pointer(df() + "extrude_temps")] = extrude_temps_conf;
    }

    auto &extrude_lengths = data[json::json_pointer(df() + "extrude_lengths")];
    if (extrude_lengths.is_null()) {
      data[json::json_pointer(df() + "extrude_lengths")] = extrude_lengths_conf;
    }

    auto &extrude_speeds = data[json::json_pointer(df() + "extrude_speeds")];
    if (extrude_speeds.is_null()) {
      data[json::json_pointer(df() + "extrude_speeds")] = extrude_speeds_conf;
    }

    auto &disable_spoolman = data[json::json_pointer(df() + "disable_spoolman")];
    if (disable_spoolman.is_null()) {
      data[json::json_pointer(df() + "disable_spoolman")] = default_disable_spoolman;
    }

    auto &guppy_init = data["/guppy_init_script"_json_pointer];
    if (guppy_init.is_null()) {
      data["/guppy_init_script"_json_pointer] = "/etc/init.d/S99guppyscreen";
    }

    auto &ll = data[json::json_pointer(df() + "log_level")];
    if (ll.is_null()) {
      data[json::json_pointer(df() + "log_level")] = "debug";
    }
  }
  auto &rotate = data["/display_rotate"_json_pointer];
  if (rotate.is_null()) {
#ifdef GUPPY_ROTATE
    data["/display_rotate"_json_pointer] = 3; // LV_DISP_ROT_270
#else
    data["/display_rotate"_json_pointer] = 0; // LV_DISP_ROT_0
#endif
  }

  auto &touch_calibrated = data["/touch_calibrated"_json_pointer];
  if (touch_calibrated.is_null()) {
#ifdef EVDEV_CALIBRATE
    data["/touch_calibrated"_json_pointer] = true; // EVDEV_CALIBRATE
#else
    data["/touch_calibrated"_json_pointer] = false; // EVDEV_CALIBRATE
#endif
  }

  auto &estop = data["/prompt_emergency_stop"_json_pointer];
  if (estop.is_null()) {
    data["/prompt_emergency_stop"_json_pointer] = true;
  }

  auto &display_sleep = data["/display_sleep_sec"_json_pointer];
  if (display_sleep.is_null()) {
    data["/display_sleep_sec"_json_pointer] = 600;
  }
  
  std::ofstream o(config_path);
  o << std::setw(2) << data << std::endl;
}

std::string& Config::df() {
  return default_printer;
}

std::string Config::get_thumbnail_path() {
  return get<std::string>("/thumbnail_path");
}

std::string Config::get_wifi_interface() {
  return fs::path(get<std::string>("/wpa_supplicant"))
    .filename()
    .string();
}

std::string Config::get_path() {
    return path;
}

json &Config::get_json(const std::string &json_path) {
  return data[json::json_pointer(json_path)];
}

void Config::save() {
  std::ofstream o(path);
  o << std::setw(2) << data << std::endl;
}
