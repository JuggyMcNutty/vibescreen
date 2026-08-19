#ifndef __BUTTON_CONTAINER_H__
#define __BUTTON_CONTAINER_H__

#include "lvgl/lvgl.h"
#include "event_guard.h"

#include <string>
#include <functional>

class ButtonContainer {
 public:
  // prompt_optional means the confirmation may be turned off by the
  // prompt_emergency_stop setting, which is what every prompt here was for
  // when that setting was added. Pass false for a confirmation that is about
  // something else and has no business being governed by it.
  ButtonContainer(lv_obj_t *parent,
		  const void *btn_img,
		  const char *text,
		  lv_event_cb_t cb,
		  void *user_data,
		  const std::string &prompt_text = {},
		  const std::function<void()> &prompt_callback = {},
		  bool prompt_optional = true);
  ~ButtonContainer();

  lv_obj_t *get_container();
  lv_obj_t *get_button();
  void disable();
  void enable();
  void hide();

  void set_image(const void *img);
  void set_label(const char *text);

  void handle_callback(lv_event_t *event);

  void handle_prompt();
  void run_callback();
  
  static void _handle_callback(lv_event_t *event) {
    KGuard::event("ButtonContainer::_handle_callback", [&] {
      ButtonContainer *button_container = (ButtonContainer*)event->user_data;
      button_container->handle_callback(event);
    });
  };

 private:
  lv_obj_t *btn_cont;
  lv_obj_t *btn;
  lv_obj_t *label;
  std::string prompt_text;
  std::function<void()> prompt_callback;
};

#endif // __BUTTON_CONTAINER_H__
