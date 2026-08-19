#include "update_check.h"

#include "config.h"
#include "subprocess.hpp"

#include "spdlog/spdlog.h"
#include "lvgl/lvgl.h"

#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>

#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

namespace {

// Same shape as UpdateDialog's Job, and for the same reason: the work happens
// on a detached thread that can outlive whatever started it, so the result
// lands behind a lock and the LVGL thread collects it from a timer. Nothing
// here touches a widget.
struct Check {
  std::mutex lock;
  bool done = false;
  std::string status;
  std::string latest;
};

std::shared_ptr<Check> in_flight;      // LVGL thread only
lv_timer_t *poll_timer = nullptr;      // LVGL thread only
lv_timer_t *collect_timer = nullptr;   // LVGL thread only

bool available = false;
std::string available_version;
std::function<void()> listener;

const uint32_t COLLECT_MS = 500;

std::string script_path() {
  try {
    const fs::path path = fs::canonical("/proc/self/exe").parent_path() / "update.sh";
    if (fs::exists(path)) {
      return path.string();
    }
  } catch (const std::exception &e) {
    spdlog::debug("update check cannot locate update.sh: {}", e.what());
  }
  return "";
}

void run_check(std::shared_ptr<Check> check, std::string script) {
  std::string out;
  try {
    // stderr is dropped rather than merged: --check contracts to print
    // key=value on stdout, and a warning from the shell landing in the middle
    // of that would be parsed as a line we do not recognise.
    sp::Popen proc({script, "--check"}, sp::output{sp::PIPE});
    FILE *pipe = proc.output();
    char buf[256];
    while (pipe != NULL && fgets(buf, sizeof(buf), pipe) != NULL) {
      out += buf;
    }
    proc.wait();
  } catch (const std::exception &e) {
    spdlog::warn("update check failed to run: {}", e.what());
  } catch (...) {
    spdlog::warn("update check failed to run");
  }

  std::string status;
  std::string latest;
  size_t pos = 0;
  while (pos < out.size()) {
    size_t eol = out.find('\n', pos);
    if (eol == std::string::npos) {
      eol = out.size();
    }
    std::string line = out.substr(pos, eol - pos);
    pos = eol + 1;

    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);
    while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) {
      value.pop_back();
    }
    if (key == "status") {
      status = value;
    } else if (key == "latest") {
      latest = value;
    }
  }

  std::lock_guard<std::mutex> guard(check->lock);
  check->status = status;
  check->latest = latest;
  check->done = true;
}

void set_available(bool now_available, const std::string &version) {
  if (available == now_available && available_version == version) {
    return;
  }
  available = now_available;
  available_version = version;
  if (listener) {
    listener();
  }
}

void collect_cb(lv_timer_t *timer) {
  auto check = in_flight;
  if (!check) {
    lv_timer_del(timer);
    collect_timer = nullptr;
    return;
  }

  std::string status;
  std::string latest;
  {
    std::lock_guard<std::mutex> guard(check->lock);
    if (!check->done) {
      return;
    }
    status = check->status;
    latest = check->latest;
  }

  in_flight = nullptr;
  lv_timer_del(timer);
  collect_timer = nullptr;

  spdlog::debug("update check says status={} latest={}", status, latest);

  // Only "available" is an offer. "devbuild" means a newer release exists but
  // update.sh would refuse to replace a local build without --force, so
  // announcing it would be advertising a button that declines to work.
  set_available(status == "available", status == "available" ? latest : "");
}

void poll_cb(lv_timer_t *) {
  UpdateCheck::check_now();
}

int32_t interval_hours() {
  auto v = Config::get_instance()->get_json("/update_check_interval_hours");
  int32_t hours = v.is_number() ? v.template get<int32_t>() : 24;
  // An interval of zero would spawn a process every tick.
  return hours < 1 ? 1 : hours;
}

bool check_enabled() {
  auto v = Config::get_instance()->get_json("/update_check_enabled");
  return v.is_null() ? true : v.template get<bool>();
}

}  // namespace

void UpdateCheck::start() {
  reconfigure();
}

void UpdateCheck::reconfigure() {
  if (poll_timer != nullptr) {
    lv_timer_del(poll_timer);
    poll_timer = nullptr;
  }

  if (!check_enabled()) {
    set_available(false, "");
    spdlog::debug("update check disabled");
    return;
  }

  if (script_path().empty()) {
    spdlog::debug("no update.sh beside the binary, update check inactive");
    return;
  }

  const uint32_t period_ms = static_cast<uint32_t>(interval_hours()) * 60u * 60u * 1000u;
  poll_timer = lv_timer_create(poll_cb, period_ms, NULL);
  spdlog::info("update check every {} hour(s)", interval_hours());

  check_now();
}

void UpdateCheck::check_now() {
  if (in_flight) {
    return;
  }

  const std::string script = script_path();
  if (script.empty()) {
    return;
  }

  auto check = std::make_shared<Check>();
  in_flight = check;
  std::thread(run_check, check, script).detach();

  if (collect_timer == nullptr) {
    collect_timer = lv_timer_create(collect_cb, COLLECT_MS, NULL);
  }
}

bool UpdateCheck::update_available() {
  return available;
}

std::string UpdateCheck::latest_version() {
  return available_version;
}

void UpdateCheck::set_listener(std::function<void()> cb) {
  listener = std::move(cb);
}
