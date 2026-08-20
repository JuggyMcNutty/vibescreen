#ifndef __KWEBSOCKET_CLIENT_H__
#define __KWEBSOCKET_CLIENT_H__

#include "hv/WebSocketClient.h"
#include "notify_consumer.h"
#include "hv/json.hpp"

#include <map>
#include <vector>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>

using json = nlohmann::json;

class KWebSocketClient : public hv::WebSocketClient {
 public:
  KWebSocketClient(hv::EventLoopPtr loop);
  ~KWebSocketClient();

  int connect(const char* url,
	      std::function<void()> connected,
	      std::function<void()> disconnected);

  void register_notify_update(NotifyConsumer *consumer);
  void unregister_notify_update(NotifyConsumer *consumer);

  int send_jsonrpc(const std::string &method, std::function<void(json&)> cb);
  int send_jsonrpc(const std::string &method, const json &params, std::function<void(json&)> cb);  
  int send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer);  
  int send_jsonrpc(const std::string &method, const json &params);
  int send_jsonrpc(const std::string &method);
  int gcode_script(const std::string &gcode);

  void register_method_callback(std::string resp_method,
				std::string handler_name,
				std::function<void(json&)> cb);

  // Called with the message text whenever Klipper rejects a gcode command.
  // Install it before connect(), it is read from the libhv thread afterwards.
  void set_error_handler(std::function<void(const std::string&)> cb);

  // Runs every handler for everything that has arrived since the last call.
  //
  // Call this from the LVGL thread and nowhere else. It is the whole point of
  // the queue: libhv's thread receives, this thread dispatches, so no handler
  // ever touches a widget from the wrong thread. See docs/audit.md C18.
  void drain();

 private:
  // Sends the request and returns, without registering anything. The id is
  // allocated by the caller so that a caller which also registers a handler can
  // do both under one lock, otherwise a concurrent send can consume the id the
  // handler was filed under and the reply is delivered to the wrong caller.
  int send_rpc(const std::string &method, const json *params, uint64_t rpc_id);

  // What arrives on libhv's event loop thread, waiting for the LVGL thread to
  // pick it up. A connection opening or closing is queued the same way a
  // message is: onopen runs on the libhv thread too, and it is the path that
  // built widgets from there and segfaulted one startup in six.
  struct Event {
    enum Kind { Message, Connected, Disconnected } kind;
    json payload;
  };

  std::mutex queue_lock;
  std::deque<Event> queue;

  // A wedged UI must not be allowed to grow this without limit on a machine
  // with 209 MB of RAM. Dropping the oldest keeps the freshest printer state,
  // at the cost of possibly dropping a reply and leaving its handler in the
  // callbacks map forever. That is a slow leak against an unbounded one, and
  // reaching this at all means the UI has been stalled for minutes.
  static constexpr size_t queue_max = 512;
  bool queue_dropped = false;

  void push(Event ev);
  void dispatch(json &j);

  // Held from connect() so drain() can invoke them. Written once, before the
  // socket opens.
  std::function<void()> on_connected;
  std::function<void()> on_disconnected;

  // Guards every container below.
  //
  // These are touched from two threads: panels call the send and register
  // methods from the LVGL thread, and libhv's onmessage dispatches from its own
  // event loop thread.
  //
  // Never invoke a handler while holding this. Two things go wrong if you do.
  // InputShaperPanel::handle_macro_response calls back into gcode_script from
  // inside a reply handler, which self deadlocks on a non recursive mutex. And
  // every consume() takes lv_lock, so holding this and then waiting on lv_lock
  // inverts the order against the LVGL thread, which holds lv_lock and can wait
  // on this from a panel destructor. Copy what you need out, unlock, then call.
  std::mutex cb_lock;

  std::map<uint64_t, std::function<void(json&)>> callbacks;
  std::map<uint64_t, NotifyConsumer*> consumers;
  std::vector<NotifyConsumer*> notify_consumers;

  // method_name : { <unique-name-cb-handler> :handler-cb }
  std::map<std::string, std::map<std::string, std::function<void(json&)>>> method_resp_cbs;
  std::atomic_uint64_t id;

  // Set once before connect(), so it needs no lock of its own.
  std::function<void(const std::string&)> error_handler;
};

#endif //__KWEBSOCKET_CLIENT_H__
