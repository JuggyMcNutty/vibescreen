#ifndef __WIDGET_HANDLE_H__
#define __WIDGET_HANDLE_H__

#include "lvgl/lvgl.h"
#include "event_guard.h"

// Keeps an lv_obj_t* member honest about whether its widget still exists.
//
// Every wrapper in this tree deletes its own container in its destructor, and
// most of those containers are children of a panel's root. Deleting that root
// deletes them as children, so the wrapper's destructor then runs lv_obj_del on
// an object LVGL has already freed. Nothing hits it today because the panels
// live for the whole process, and it becomes real the moment anything is torn
// down, which is what multi-printer switching wants to do. docs/audit.md C11.
//
// MeshView solved it first by nulling its handle from LV_EVENT_DELETE. This is
// that, once, so the other sixteen do not each carry a copy.
namespace KWidget {
  inline void null_on_delete(lv_obj_t **handle) {
    lv_obj_add_event_cb(*handle, [](lv_event_t *e) {
      KGuard::event("KWidget::null_on_delete", [&] {
	lv_obj_t **h = static_cast<lv_obj_t **>(lv_event_get_user_data(e));
	// A child carrying LV_OBJ_FLAG_EVENT_BUBBLE sends its own delete up to
	// us as well, and that one must not null the handle: the container is
	// still there, and skipping its delete would leak it instead.
	if (lv_event_get_target(e) == *h) {
	  *h = NULL;
	}
      });
    }, LV_EVENT_DELETE, handle);
  }
}

#endif // __WIDGET_HANDLE_H__
