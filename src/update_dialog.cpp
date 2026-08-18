#include "update_dialog.h"

#include "event_guard.h"
#include "subprocess.hpp"
#include "spdlog/spdlog.h"
#include "lvgl/lvgl.h"

#include <cstdio>
#include <experimental/filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::experimental::filesystem;
namespace sp = subprocess;

namespace {

// What the thread running the script and the timer drawing its output share.
// Both hold it by shared_ptr because neither owns the other: the dialog can be
// closed while the script runs on, and the run outlives the dialog so opening
// it again shows the same output rather than starting a second update.
struct Job {
  std::mutex lock;
  std::string output;
  bool done = false;
  int rc = 0;
};

// LVGL thread only, so it needs no lock of its own.
std::shared_ptr<Job> running_job;

void append(const std::shared_ptr<Job> &job, const std::string &s) {
  std::lock_guard<std::mutex> guard(job->lock);
  job->output += s;
}

void finish(const std::shared_ptr<Job> &job, int rc) {
  std::lock_guard<std::mutex> guard(job->lock);
  job->rc = rc;
  job->done = true;
}

bool is_running(const std::shared_ptr<Job> &job) {
  if (job == NULL) {
    return false;
  }
  std::lock_guard<std::mutex> guard(job->lock);
  return !job->done;
}

// The worker. Touches nothing but the job: LVGL is not thread safe, and taking
// lv_lock here would put this thread ahead of the main loop for the length of a
// download.
void run_script(std::shared_ptr<Job> job, std::string script) {
  int rc = 1;
  try {
    // stderr into stdout, so the reason a failure gives is shown next to the
    // step it failed at rather than only reaching the log.
    sp::Popen proc({script}, sp::output{sp::PIPE}, sp::error{sp::STDOUT});
    FILE *out = proc.output();
    char buf[256];
    while (out != NULL && fgets(buf, sizeof(buf), out) != NULL) {
      append(job, buf);
    }
    rc = proc.wait();
  } catch (const std::exception &e) {
    append(job, fmt::format("\nCould not run {}: {}\n", script, e.what()));
  } catch (...) {
    append(job, fmt::format("\nCould not run {}\n", script));
  }
  finish(job, rc);
}

struct Dialog {
  lv_obj_t *box = NULL;
  lv_obj_t *text = NULL;
  lv_obj_t *spinner = NULL;
  lv_timer_t *timer = NULL;
  std::shared_ptr<Job> job;
  // How much of the output is already on screen, so a poll that found nothing
  // new does not rebuild the label and fight the user's scrolling.
  size_t shown = 0;
  bool shown_done = false;
};

void refresh(Dialog *d) {
  std::string out;
  bool done;
  int rc;
  {
    std::lock_guard<std::mutex> guard(d->job->lock);
    out = d->job->output;
    done = d->job->done;
    rc = d->job->rc;
  }

  if (out.size() == d->shown && done == d->shown_done) {
    return;
  }
  d->shown = out.size();
  d->shown_done = done;

  if (done) {
    // Say the outcome in the log as well as the title. The log is what the
    // reader is looking at, and it is the part that stays put when a long run
    // has scrolled the title out of view.
    out += rc == 0 ? "\nUpdate finished."
                   : fmt::format("\nUpdate failed, exit code {}.", rc);
    lv_label_set_text(lv_msgbox_get_title(d->box),
                      rc == 0 ? "Update finished" : "Update failed");
    // The spinner is hidden rather than deleted and the timer paused rather
    // than deleted, because both go away with the dialog anyway and neither
    // needs LVGL to tear an object down from inside a timer callback.
    if (d->spinner != NULL) {
      lv_obj_add_flag(d->spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (d->timer != NULL) {
      lv_timer_pause(d->timer);
    }
  }

  lv_label_set_text(d->text, out.c_str());

  // Follow the tail the way a log window does, so the newest line is the one
  // being read. The label's own container and not the whole msgbox: scrolling
  // that takes the title with it, and the title is where the outcome is said.
  lv_obj_t *content = lv_obj_get_parent(d->text);
  // The label only grew a moment ago and its new height is not in its
  // coordinates until the layout runs, so without this the scroll always
  // stops one update short and the last line to arrive is the one nobody
  // sees.
  lv_obj_update_layout(content);
  const lv_coord_t below = lv_obj_get_scroll_bottom(content);
  if (below > 0) {
    lv_obj_scroll_by(content, 0, -below, LV_ANIM_OFF);
  }
}

void poll_cb(lv_timer_t *timer) {
  KGuard::event("UpdateDialog::poll_cb", [&] {
    refresh(static_cast<Dialog *>(timer->user_data));
  });
}

void open_dialog(const std::shared_ptr<Job> &job) {
  Dialog *d = new Dialog();
  d->job = job;

  std::string opening;
  {
    std::lock_guard<std::mutex> guard(job->lock);
    opening = job->output;
  }
  // Both the title and the text have to be non-empty here. lv_msgbox_create
  // only builds the label when the string it is given has a length, so an
  // empty one leaves lv_msgbox_get_text returning NULL, and the
  // lv_obj_set_width below then spins the LVGL thread forever with no way back.
  if (opening.empty()) {
    opening = "Working";
  }

  static const char *btns[] = {"Close", ""};
  // Close stays available throughout. A modal dialog that cannot be dismissed
  // would also lock out the emergency stop for as long as the download takes,
  // and the run carries on in the background anyway.
  d->box = lv_msgbox_create(NULL, "Updating Guppy", opening.c_str(), btns, false);

  d->text = lv_msgbox_get_text(d->box);
  lv_obj_set_width(d->text, LV_PCT(100));
  lv_label_set_long_mode(d->text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_font(d->text, &lv_font_montserrat_12, 0);

  const double hscale = (double)lv_disp_get_physical_ver_res(NULL) / 480.0;

  const lv_coord_t btn_h = 50 * hscale;
  lv_obj_t *btnm = lv_msgbox_get_btns(d->box);
  lv_obj_add_flag(btnm, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(btnm, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_size(btnm, LV_PCT(90), btn_h);

  lv_obj_set_size(d->box, LV_PCT(80), LV_PCT(60));
  lv_obj_center(d->box);

  // The log takes what is left between the title and the buttons and scrolls
  // inside it. A msgbox sizes its content to the text instead, which for a log
  // that grows means the newest lines run out through the bottom of the dialog
  // and under the buttons, which float and so reserve no space of their own.
  lv_obj_update_layout(d->box);
  lv_obj_t *content = lv_obj_get_parent(d->text);
  const lv_coord_t log_h = lv_obj_get_content_height(d->box)
      - lv_obj_get_height(lv_msgbox_get_title(d->box)) - btn_h
      - lv_obj_get_style_pad_row(d->box, LV_PART_MAIN);
  lv_obj_set_height(content, LV_MAX(log_h, btn_h));

  // Floating, or it would be laid out into the flex row and push the log
  // around as it comes and goes.
  d->spinner = lv_spinner_create(d->box, 1000, 60);
  lv_obj_add_flag(d->spinner, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_size(d->spinner, 24 * hscale, 24 * hscale);
  lv_obj_align(d->spinner, LV_ALIGN_TOP_RIGHT, 0, 0);

  lv_obj_add_event_cb(d->box, [](lv_event_t *e) {
    KGuard::event("UpdateDialog::close", [&] {
      // Async on purpose. The event comes from the button matrix and bubbles
      // up to the msgbox, so lv_btnmatrix_event is still on the stack and goes
      // on using the button matrix that a plain close would already have
      // freed. That is what lv_msgbox_close_async exists for.
      lv_msgbox_close_async(static_cast<Dialog *>(lv_event_get_user_data(e))->box);
    });
  }, LV_EVENT_VALUE_CHANGED, d);

  lv_obj_add_event_cb(d->box, [](lv_event_t *e) {
    KGuard::event("UpdateDialog::deleted", [&] {
      Dialog *d = static_cast<Dialog *>(lv_event_get_user_data(e));
      if (d->timer != NULL) {
        lv_timer_del(d->timer);
      }
      // Only the dialog goes. The job keeps the thread's shared_ptr alive and
      // the script runs to the end whether or not anyone is watching.
      delete d;
    });
  }, LV_EVENT_DELETE, d);

  d->timer = lv_timer_create(&poll_cb, 200, d);
  // Whatever the job has said already, including all of it if this is a second
  // look at a run that has finished.
  refresh(d);
}

}  // namespace

void UpdateDialog::show() {
  if (is_running(running_job)) {
    // A second press while the first is still downloading shows that run
    // rather than starting another one over the top of it.
    open_dialog(running_job);
    return;
  }

  auto job = std::make_shared<Job>();
  running_job = job;

  std::string script;
  try {
    const fs::path path = fs::canonical("/proc/self/exe").parent_path() / "update.sh";
    if (fs::exists(path)) {
      script = path.string();
    } else {
      append(job, fmt::format("No update script at {}.\n\nGuppy updates itself "
                              "with the update.sh installed beside it.\n", path.string()));
    }
  } catch (const std::exception &e) {
    append(job, fmt::format("Could not work out where the update script is: {}\n", e.what()));
  }

  if (script.empty()) {
    spdlog::warn("update guppy pressed with no update script to run");
    finish(job, 1);
  } else {
    append(job, "Running update.sh. Guppy restarts itself if it installs a new version.\n\n");
    std::thread(run_script, job, script).detach();
  }

  open_dialog(job);
}
