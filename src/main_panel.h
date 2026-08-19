#ifndef __MAIN_PANEL_H__
#define __MAIN_PANEL_H__

#include "websocket_client.h"
#include "event_guard.h"
#include "notify_consumer.h"
#include "sensor_container.h"
#include "button_container.h"
#include "prompt_panel.h"
#include "numpad.h"
#include "homing_panel.h"
#include "extruder_panel.h"
#include "fan_panel.h"
#include "led_panel.h"
#include "print_panel.h"
#include "macros_panel.h"
#include "console_panel.h"
#include "printertune_panel.h"
#include "setting_panel.h"
#include "print_status_panel.h"
#include "spoolman_panel.h"
#include "update_check.h"
#include "lvgl/lvgl.h"

#include <mutex>
#include <map>
#include <memory>

class MainPanel : public NotifyConsumer {
 public:
  MainPanel(KWebSocketClient &ws,
	    std::mutex &lv_lock,
	    SpoolmanPanel &sm);

  ~MainPanel();
  void consume(json &data);
  void init(json &data);
  void subscribe();
  PrinterTunePanel& get_tune_panel();
  void enable_spoolman();
  
  void create_panel();
  void create_sensors(json &temp_sensors);
  void create_fans(json &temp_fans);
  void create_leds(json &leds);
  void handle_homing_cb(lv_event_t *event);
  void handle_extrude_cb(lv_event_t *event);
  void handle_fanpanel_cb(lv_event_t *event);
  void handle_ledpanel_cb(lv_event_t *event);
  void handle_print_cb(lv_event_t *event);

  lv_obj_t *create_button(lv_obj_t *parent,
			  const void *btn_img,
			  const char* text,
			  lv_event_cb_t cb);

  lv_obj_t *create_heater_info(lv_obj_t *parent,
			       const void *heater_img,
			       const char* text,
			       lv_color_t color);
  
  static void _handle_homing_cb(lv_event_t *event) {
    KGuard::event("MainPanel::_handle_homing_cb", [&] {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_homing_cb(event);
    });
  };

  static void _handle_extrude_cb(lv_event_t *event) {
    KGuard::event("MainPanel::_handle_extrude_cb", [&] {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_extrude_cb(event);
    });
  };

  static void _handle_fanpanel_cb(lv_event_t *event) {
    KGuard::event("MainPanel::_handle_fanpanel_cb", [&] {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_fanpanel_cb(event);
    });
  };

  static void _handle_ledpanel_cb(lv_event_t *event) {
    KGuard::event("MainPanel::_handle_ledpanel_cb", [&] {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_ledpanel_cb(event);
    });
  };

  static void _handle_print_cb(lv_event_t *event) {
    KGuard::event("MainPanel::_handle_print_cb", [&] {
      MainPanel *panel = (MainPanel*)event->user_data;
      panel->handle_print_cb(event);
    });
  };

 private:
  void create_main(lv_obj_t *parent);
  void refresh_update_badge();
  KWebSocketClient &ws;
  HomingPanel homing_panel;
  FanPanel fan_panel;
  LedPanel led_panel;
  lv_obj_t *tabview;
  lv_obj_t *main_tab;
  lv_obj_t *macros_tab;
  MacrosPanel macros_panel;
  lv_obj_t *console_tab;
  ConsolePanel console_panel;
  lv_obj_t *printertune_tab;
  lv_obj_t *setting_tab;
  SettingPanel setting_panel;
  lv_obj_t *main_cont;
  PrintStatusPanel print_status_panel;
  PrintPanel print_panel;
  PrinterTunePanel printertune_panel;
  Numpad numpad;
  ExtruderPanel extruder_panel;
  PromptPanel prompt_panel;
  SpoolmanPanel &spoolman_panel;
  
  lv_style_t style;

  // A dot over the settings icon in the rail, shown when UpdateCheck finds a
  // newer release. Settings is the last tab, so it anchors to the bottom of the
  // tab strip.
  lv_obj_t *update_badge;

  lv_obj_t *temp_cont;
  lv_obj_t *temp_chart;

  std::map<std::string, std::shared_ptr<SensorContainer>> sensors;
  
  ButtonContainer homing_btn;
  ButtonContainer extrude_btn;
  ButtonContainer action_btn;
  ButtonContainer led_btn;
  ButtonContainer print_btn;
};
#endif // __MAIN_PANEL_H__
