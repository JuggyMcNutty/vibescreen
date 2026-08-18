#include "guppyscreen.h"

#include "config.h"
#include "utils.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_sinks.h"

#include "printer_select_panel.h"
#include "spdlog/spdlog.h"
#include "state.h"
#include "theme.h"

#include <time.h>

GuppyScreen *GuppyScreen::instance = NULL;
lv_style_t GuppyScreen::style_container;
lv_style_t GuppyScreen::style_imgbtn_default;
lv_style_t GuppyScreen::style_imgbtn_pressed;
lv_style_t GuppyScreen::style_imgbtn_disabled;
lv_theme_t GuppyScreen::th_new;

lv_obj_t *GuppyScreen::screen_saver = NULL;

KWebSocketClient GuppyScreen::ws(NULL);

std::mutex GuppyScreen::lv_lock;
lv_obj_t *GuppyScreen::error_box = NULL;

GuppyScreen::GuppyScreen()
  : spoolman_panel(ws, lv_lock)
  , main_panel(ws, lv_lock, spoolman_panel)
  , init_panel(main_panel, main_panel.get_tune_panel().get_bedmesh_panel(), lv_lock)
{
  main_panel.create_panel();
}

GuppyScreen *GuppyScreen::get() {
  if (instance == NULL) {
    instance = new GuppyScreen();
  }

  return instance;
}

GuppyScreen *GuppyScreen::init(std::function<void(lv_color_t, lv_color_t)> hal_init) {
  hlog_disable();

  // config
  Config *conf = Config::get_instance();
  const std::string ll_path = conf->df() + "log_level";
  auto ll = spdlog::level::from_str(
      conf->get_json("/printers").empty() 
      ? "debug" 
      : conf->get<std::string>(ll_path));

  auto selected_theme = conf->get_json("/theme").empty()
          ? "blue.json"
          : conf->get<std::string>("/theme") + ".json";
  auto theme_config = fs::canonical(conf->get_path()).parent_path() / "themes" / selected_theme;

  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  theme_conf->init(theme_config);

  auto primary_color = theme_conf->get_json("/primary_color").empty()
          ? lv_color_hex(0x2196F3)
          : lv_color_hex(KUtils::parse_hex(theme_conf->get<std::string>("/primary_color"), 0x2196F3));

  auto secondary_color = theme_conf->get_json("/secondary_color").empty()
          ? lv_color_hex(0xF44336)
          : lv_color_hex(KUtils::parse_hex(theme_conf->get<std::string>("/secondary_color"), 0xF44336));

  auto console_sink = std::make_shared<spdlog::sinks::stdout_sink_mt>();
  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      conf->get<std::string>("/log_path"), 1048576 * 10, 3);
  spdlog::sinks_init_list log_sinks{console_sink, file_sink};

  auto klogger = std::make_shared<spdlog::logger>("guppyscreen", log_sinks);
  spdlog::register_logger(klogger);

  spdlog::set_level(ll);
  spdlog::set_default_logger(klogger);
  klogger->flush_on(ll);

#ifdef GUPPYSCREEN_VERSION
  spdlog::info("Guppy Screen Version: {}", GUPPYSCREEN_VERSION);
#endif  // GUPPYSCREEN_VERSION

  spdlog::info("DPI: {}", LV_DPI_DEF);
  /*LittlevGL init*/
  lv_init();

#ifndef SIMULATOR
  /*Linux frame buffer device init*/
  fbdev_init();
  fbdev_unblank();
#endif  // SIMULATOR

  hal_init(primary_color, secondary_color);
  lv_png_init();

  lv_style_init(&style_container);
  lv_style_set_border_width(&style_container, 0);
  lv_style_set_radius(&style_container, 0);

//  lv_style_init(&style_imgbtn_default);
//  lv_style_set_img_recolor_opa(&style_imgbtn_default, LV_OPA_100);
//  lv_style_set_img_recolor(&style_imgbtn_default, lv_color_black());

  lv_style_init(&style_imgbtn_pressed);
  lv_style_set_img_recolor_opa(&style_imgbtn_pressed, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);

  lv_style_init(&style_imgbtn_disabled);
  lv_style_set_img_recolor_opa(&style_imgbtn_disabled, LV_OPA_100);
  lv_style_set_img_recolor(&style_imgbtn_disabled, lv_palette_darken(LV_PALETTE_GREY, 1));

  /*Initia1ize the new theme from the current theme*/

  lv_theme_t *th_act = lv_disp_get_theme(NULL);
  th_new = *th_act;

  /*Set the parent theme and the style apply callback for the new theme*/
  lv_theme_set_parent(&th_new, th_act);
  lv_theme_set_apply_cb(&th_new, &GuppyScreen::new_theme_apply_cb);

  /*Assign the new theme to the current display*/
  lv_disp_set_theme(NULL, &th_new);

  ws.register_notify_update(State::get_instance());

  GuppyScreen *gs = GuppyScreen::get();
  auto printers = conf->get_json("/printers");
  if (!printers.empty()) {
    // start initializing all guppy components
    std::string ws_url = fmt::format("ws://{}:{}/websocket",
                                     conf->get<std::string>(conf->df() + "moonraker_host"),
                                     conf->get<uint32_t>(conf->df() + "moonraker_port"));

    spdlog::info("connecting to printer at {}", ws_url);
    gs->connect_ws(ws_url);
  }

  screen_saver = lv_obj_create(lv_scr_act());

  lv_obj_set_size(screen_saver, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_opa(screen_saver, LV_OPA_100, 0);
  lv_obj_move_background(screen_saver);

  lv_obj_t *main_screen = lv_disp_get_scr_act(NULL);
  auto touch_calibrated = conf->get_json("/touch_calibrated");
  if (!touch_calibrated.is_null()) {
    auto is_calibrated = touch_calibrated.template get<bool>();
    if (is_calibrated) {
      auto calibration_coeff = conf->get_json("/touch_calibration_coeff");
      if (calibration_coeff.is_null()) {
        lv_tc_register_coeff_save_cb(&GuppyScreen::save_calibration_coeff);
        lv_obj_t *touch_calibrate_scr = lv_tc_screen_create();

        lv_disp_load_scr(touch_calibrate_scr);

        lv_tc_screen_start(touch_calibrate_scr);
        lv_obj_add_event_cb(touch_calibrate_scr, &GuppyScreen::handle_calibrated, LV_EVENT_READY, main_screen);
        spdlog::info("running touch calibration");
      } else {
        // load calibration data
        auto c = calibration_coeff.template get<std::vector<float>>();
        lv_tc_coeff_t coeff = {true, c[0], c[1], c[2], c[3], c[4], c[5]};
        lv_tc_set_coeff(coeff, false);
        spdlog::info("loaded calibration coefficients");
      }
    }
  }

  return gs;
}

void GuppyScreen::loop() {
  /*Handle LitlevGL tasks (tickless mode)*/
#ifndef SIMULATOR
  std::atomic_bool is_sleeping(false);
  Config *conf = Config::get_instance();

  // -1 is the Never sentinel the system panel writes. Scaling it first and then
  // testing for -1 never matched, so the disable branch below was dead: what
  // made Never work was the comparison promoting -1000 to an unsigned 49.7 day
  // threshold. That is not a timeout anyone chose, and it would sleep instantly
  // rather than never once the tick wrapped.
  int32_t sleep_sec = conf->get<int32_t>("/display_sleep_sec");
  int32_t display_sleep = sleep_sec < 0 ? -1 : sleep_sec * 1000;
#endif

  while (1) {
    // Exceptions are contained at the event callbacks themselves, see
    // KGuard::event in event_guard.h. Nothing should reach here.
    //
    // If something does, LVGL is already unrecoverable: its timer re-entrancy
    // guard is left set and its input state machine is interrupted mid gesture,
    // so carrying on gives a frozen UI that re-dispatches the same event and
    // floods the log. Both were measured. Log what happened and let the process
    // die so the init script restarts it, which is the better failure mode on a
    // printer.
    //
    // The lock_guard is not decoration: this was a bare lock and unlock pair, so
    // a throw would have skipped the unlock and deadlocked the UI.
    try {
      std::lock_guard<std::mutex> guard(lv_lock);
      lv_timer_handler();
    } catch (const std::exception &e) {
      spdlog::critical("exception reached the main loop, {}. lvgl cannot be "
		       "recovered from here, exiting so we get restarted", e.what());
      spdlog::shutdown();
      std::abort();
    } catch (...) {
      spdlog::critical("unknown exception reached the main loop, exiting so we get restarted");
      spdlog::shutdown();
      std::abort();
    }

#ifndef SIMULATOR
    if (display_sleep != -1) {
      if (lv_disp_get_inactive_time(NULL) > display_sleep) {
        if (!is_sleeping.load()) {
          spdlog::debug("putting display to sleeping");
          fbdev_blank();
          lv_obj_move_foreground(screen_saver);
          // spdlog::debug("screen saver foreground");
          is_sleeping = true;
        }
      } else {
        if (is_sleeping.load()) {
          spdlog::debug("waking up display");
          fbdev_unblank();
          lv_obj_move_background(screen_saver);
          is_sleeping = false;
        }
      }
    }
#endif  // SIMULATOR

    usleep(5000);
  }
}

void GuppyScreen::show_error(const std::string &message) {
  // Called from the libhv thread, so take the UI lock before touching LVGL.
  // Safe against the websocket client's own lock because handlers are always
  // invoked after it has been released, see the note on cb_lock.
  std::lock_guard<std::mutex> guard(lv_lock);

  // Coalesce. A run of rejections must not stack a dialog per failure, which
  // would bury the screen and need one tap each to clear.
  if (error_box != NULL) {
    lv_label_set_text(lv_msgbox_get_text(error_box), message.c_str());
    return;
  }

  static const char *btns[] = {"Close", ""};
  error_box = lv_msgbox_create(NULL, "Printer rejected the command",
			       message.c_str(), btns, false);

  lv_obj_t *text = lv_msgbox_get_text(error_box);
  lv_obj_set_width(text, LV_PCT(100));
  lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);

  lv_obj_t *btnm = lv_msgbox_get_btns(error_box);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);

  auto hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;
  lv_obj_set_size(btnm, LV_PCT(90), 50 * hscale);
  lv_obj_set_size(error_box, LV_PCT(80), LV_PCT(45));
  lv_obj_center(error_box);

  // Async, or the delete happens with lv_btnmatrix_event still on the stack
  // and still using the button matrix that closing frees.
  lv_obj_add_event_cb(error_box, [](lv_event_t *e) {
    lv_msgbox_close_async(lv_obj_get_parent(lv_event_get_target(e)));
  }, LV_EVENT_VALUE_CHANGED, NULL);

  // Clear the handle however it goes away, so the next error can open a new one.
  lv_obj_add_event_cb(error_box, [](lv_event_t *) {
    GuppyScreen::error_box = NULL;
  }, LV_EVENT_DELETE, NULL);
}

std::mutex &GuppyScreen::get_lock() {
    return lv_lock;
}

void GuppyScreen::connect_ws(const std::string &url) {
  init_panel.set_message(LV_SYMBOL_WARNING " Waiting for printer to initialize...");

  // Installed before connect so it is never read while being written.
  ws.set_error_handler([](const std::string &message) {
    GuppyScreen::show_error(message);
  });

  ws.connect(url.c_str(),
   [this]() { init_panel.connected(ws); },
   [this]() { init_panel.disconnected(ws); });
}

void GuppyScreen::new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj) {
  LV_UNUSED(th);

  if (lv_obj_check_type(obj, &lv_obj_class)) {
    lv_obj_add_style(obj, &style_container, 0);
  }

  if (lv_obj_check_type(obj, &lv_imgbtn_class)) {
//    lv_obj_add_style(obj, &style_imgbtn_default, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &style_imgbtn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(obj, &style_imgbtn_disabled, LV_STATE_DISABLED);
  }
}

void GuppyScreen::handle_calibrated(lv_event_t *event) {
  spdlog::info("finished calibration");
  lv_obj_t *main_screen = (lv_obj_t *)event->user_data;
  lv_disp_load_scr(main_screen);
}

void GuppyScreen::save_calibration_coeff(lv_tc_coeff_t coeff) {
  Config *conf = Config::get_instance();
  conf->set<std::vector<float>>("/touch_calibration_coeff",
                                {coeff.a, coeff.b, coeff.c, coeff.d, coeff.e, coeff.f});
  conf->save();
}

void GuppyScreen::refresh_theme() {
  lv_theme_t *th = lv_theme_default_get();
  ThemeConfig *theme_conf = ThemeConfig::get_instance();
  auto primary_color = theme_conf->get_json("/primary_color").empty()
                       ? lv_color_hex(0x2196F3)
                       : lv_color_hex(KUtils::parse_hex(theme_conf->get<std::string>("/primary_color"), 0x2196F3));

  auto secondary_color = theme_conf->get_json("/secondary_color").empty()
                         ? lv_color_hex(0xF44336)
                         : lv_color_hex(KUtils::parse_hex(theme_conf->get<std::string>("/secondary_color"), 0xF44336));

  lv_disp_t *disp = lv_disp_get_default();
  lv_theme_t * new_theme =  lv_theme_default_init(disp, primary_color, secondary_color, true, th->font_normal);
  lv_disp_set_theme(disp, new_theme);
  lv_style_set_img_recolor(&style_imgbtn_pressed, primary_color);
}

/*Set in lv_conf.h as `LV_TICK_CUSTOM_SYS_TIME_EXPR`*/
//
// Everything LVGL times runs off this: every animation, every timer, the
// display sleep countdown and the input shaper's watchdog. It used to read
// gettimeofday, which is the wall clock, so an NTP step moved all of them at
// once. On the K1 that is not hypothetical, ntpd steps the clock at boot
// because the machine has no RTC.
//
// It also computed tv_sec * 1000000 in time_t. Where time_t is 32 bits, which
// is every glibc mips build including the one upstream's last tagged release
// was made with, that overflows with a period of 2^32/10^6 seconds, or 71.6
// minutes, and the tick sawtooths. Feed that to lv_tick_elaps and the display
// sleeps immediately and wakes again exactly one sawtooth later, which is the
// "screen turns itself on about every hour" of upstream #80 and #7, at the
// right period. Our musl toolchain has a 64 bit time_t so it does not fire
// here, but the expression is signed overflow either way.
//
// CLOCK_MONOTONIC is what a tick source wants: it does not move when the wall
// clock does and it counts from boot, so the only wrap left is the 32 bit
// millisecond one LVGL already handles in lv_tick_elaps.
uint32_t custom_tick_get(void) {
  static struct timespec start = {0, 0};
  if (start.tv_sec == 0 && start.tv_nsec == 0) {
    clock_gettime(CLOCK_MONOTONIC, &start);
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  return (uint32_t)((now.tv_sec - start.tv_sec) * 1000
		    + (now.tv_nsec - start.tv_nsec) / 1000000);
}
