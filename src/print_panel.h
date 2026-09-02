#ifndef __PRINT_PANEL_H__
#define __PRINT_PANEL_H__

#include "lvgl/lvgl.h"
#include "event_guard.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "file_panel.h"
#include "print_status_panel.h"
#include "tree.h"

#include <atomic>

class PrintPanel : public NotifyConsumer {
 public:
  PrintPanel(KWebSocketClient &ws, std::mutex &lv_lock, PrintStatusPanel &ps);
  ~PrintPanel();

  void consume(json &data);
  void subscribe();
  void handle_filelist_changed(json &j);
  void foreground();
  void handle_callback(lv_event_t *event);
  void handle_metadata(Tree *, json & data);
  void handle_back_btn(lv_event_t *event);
  void handle_print_callback(lv_event_t *event);
  void handle_status_btn(lv_event_t *event);
  void handle_btns(lv_event_t *event);
  
  static void _handle_callback(lv_event_t *event) {
    KGuard::event("PrintPanel::_handle_callback", [&] {
      PrintPanel *panel = (PrintPanel*)event->user_data;
      panel->handle_callback(event);
    });
  };

  static void _handle_back_btn(lv_event_t *event) {
    KGuard::event("PrintPanel::_handle_back_btn", [&] {
      PrintPanel *panel = (PrintPanel*)event->user_data;
      panel->handle_back_btn(event);
    });
  };
  
  static void _handle_print_callback(lv_event_t *event) {
    KGuard::event("PrintPanel::_handle_print_callback", [&] {
      PrintPanel *panel = (PrintPanel*)event->user_data;
      panel->handle_print_callback(event);
    });
  };

  static void _handle_status_btn(lv_event_t *event) {
    KGuard::event("PrintPanel::_handle_status_btn", [&] {
      PrintPanel *panel = (PrintPanel*)event->user_data;
      panel->handle_status_btn(event);
    });
  };

  static void _handle_btns(lv_event_t *event) {
    KGuard::event("PrintPanel::_handle_btns", [&] {
      PrintPanel *panel = (PrintPanel*)event->user_data;
      panel->handle_btns(event);
    });
  };

  static void _handle_refresh_timer(lv_timer_t *timer) {
    KGuard::event("PrintPanel::_handle_refresh_timer", [&] {
      PrintPanel *panel = (PrintPanel*)timer->user_data;
      panel->handle_refresh_timer();
    });
  };
  
  
 private:
  // Why the table is being rewritten. A user asking for a different view wants
  // to be taken to the top of it; a refetch behind their back must leave them
  // where they were, which is issue #5.
  enum class ListChange { UserAction, Refetch };

  void populate_files();
  void handle_refresh_timer();
  void show_dir(Tree *dir, ListChange why);
  void show_file_detail(Tree *f);
  
  KWebSocketClient &ws;
  lv_obj_t *files_cont;

  // prompt
  lv_obj_t *prompt_cont;
  lv_obj_t *msgbox;
  lv_obj_t *job_btn;
  lv_obj_t *cancel_btn;
  lv_obj_t *queue_btn;

  lv_obj_t *left_cont;
  lv_obj_t *file_table_btns;
  lv_obj_t *refresh_btn;
  lv_obj_t *modified_sort_btn;
  lv_obj_t *az_sort_btn;
  
  lv_obj_t *file_table;
  lv_obj_t *file_view;
  ButtonContainer status_btn;
  ButtonContainer print_btn;
  ButtonContainer back_btn;
  Tree root;
  Tree *cur_dir;
  Tree *cur_file;
  FilePanel file_panel;
  PrintStatusPanel &print_status;

  // Which column the list is sorted on and in which direction, kept apart
  // because one bit field holding both could not say "modified, descending"
  // without also reading as "sort by name".
  uint32_t sort_column;
  bool sort_reversed;

  // Set on the websocket thread by handle_filelist_changed, read and cleared by
  // the timer on the LVGL thread. An upload of several files announces each one
  // and the refetch is the whole gcodes list, so they are worth coalescing.
  std::atomic<bool> refresh_pending;
  lv_timer_t *refresh_timer;
};

#endif // __PRINT_PANEL_H__
