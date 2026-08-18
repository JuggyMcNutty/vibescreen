#ifndef __UPDATE_DIALOG_H__
#define __UPDATE_DIALOG_H__

// Runs the updater and shows what it is doing.
//
// The Update Guppy button used to call update.sh straight from its event
// handler. That ran the download on the LVGL thread, so the screen froze for as
// long as it took with nothing on it to say why, and then either vanished as
// the script restarted the app, or came back with no word of what happened. Up
// to date, no network, and a dead button all looked identical.
class UpdateDialog {
 public:
  // Opens the dialog, starting update.sh unless a run is already going, in
  // which case it shows that one. LVGL thread only.
  static void show();
};

#endif // __UPDATE_DIALOG_H__
