/*
 * websocket client
 *
 * @build   make examples
 * @server  bin/websocket_server_test 8888
 * @client  bin/websocket_client_test ws://127.0.0.1:8888/test
 * @clients bin/websocket_client_test ws://127.0.0.1:8888/test 100
 * @python  scripts/websocket_server.py
 * @js      html/websocket_client.html
 *
 */

#include "websocket_client.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>

using namespace hv;
using json = nlohmann::json;

KWebSocketClient::KWebSocketClient(EventLoopPtr loop)
  : WebSocketClient(loop)
  , id(0)
{
}

KWebSocketClient::~KWebSocketClient() {
}

int KWebSocketClient::connect(const char* url,
			      std::function<void()> connected,
			      std::function<void()> disconnected) {
  spdlog::debug("websocket connecting");
  // set callbacks
  onopen = [this, connected]() {
    const HttpResponsePtr& resp = getHttpResponse();
    spdlog::debug("onopen {}", resp->body.c_str());
    connected();
  };
  onmessage = [this, connected, disconnected](const std::string &msg) {
    // This runs on libhv's event loop thread, not the LVGL thread. Nothing
    // above us catches, so an escaping exception here would take the process
    // down, and json::parse throws on anything malformed.
    try {
      json j = json::parse(msg);

      if (j.contains("id")) {
        uint64_t rpc_id = j["id"].template get<uint64_t>();

        // Take the handlers out under the lock, then release before invoking.
        // See the note on cb_lock in the header for why this matters.
        NotifyConsumer *consumer = nullptr;
        std::function<void(json&)> cb;
        {
          std::lock_guard<std::mutex> guard(cb_lock);

          // XXX: get rid of consumers and use function ptrs for callback
          const auto &entry = consumers.find(rpc_id);
          if (entry != consumers.end()) {
            consumer = entry->second;
            consumers.erase(entry);
          }

          const auto &cb_entry = callbacks.find(rpc_id);
          if (cb_entry != callbacks.end()) {
            cb = cb_entry->second;
            callbacks.erase(cb_entry);
          }
        }

        if (consumer != nullptr) {
          consumer->consume(j);
        }
        if (cb) {
          cb(j);
        }
      }

      if (j.contains("method")) {
        std::string method = j["method"].template get<std::string>();

        std::vector<NotifyConsumer*> status_consumers;
        std::vector<std::function<void(json&)>> method_cbs;
        {
          std::lock_guard<std::mutex> guard(cb_lock);
          if ("notify_status_update" == method) {
            status_consumers = notify_consumers;
          }

          const auto &entry = method_resp_cbs.find(method);
          if (entry != method_resp_cbs.end()) {
            for (const auto &handler_entry : entry->second) {
              method_cbs.push_back(handler_entry.second);
            }
          }
        }

        for (const auto &entry : status_consumers) {
          entry->consume(j);
        }

        if ("notify_klippy_disconnected" == method) {
          disconnected();
        } else if ("notify_klippy_ready" == method) {
          connected();
        }

        for (const auto &cb : method_cbs) {
          cb(j);
        }
      }
    } catch (const std::exception &e) {
      spdlog::error("dropping websocket message, {}", e.what());
    }
  };

  onclose = [disconnected]() {
    spdlog::debug("onclose");
    disconnected();
  };

  // ping
  setPingInterval(10000);

  reconn_setting_t reconn;
  reconn_setting_init(&reconn);
  reconn.min_delay = 200;
  reconn.max_delay = 2000;
  reconn.delay_policy = 2;
  setReconnect(&reconn);

  // Moonraker's authorization component rejects an anonymous websocket when
  // force_logins or a trusted client list is in use, and this connection was
  // always anonymous: guppyconfig.json has carried moonraker_api_key since
  // upstream and nothing ever read it. Upstream #32.
  http_headers headers;
  auto api_key = KUtils::moonraker_api_key();
  if (!api_key.empty()) {
    spdlog::debug("sending moonraker api key with the websocket handshake");
    headers["X-Api-Key"] = api_key;
  }

  return open(url, headers);
};

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   const json &params,
				   std::function<void(json&)> cb) {
  uint64_t rpc_id;
  {
    std::lock_guard<std::mutex> guard(cb_lock);
    rpc_id = id++;
    // XXX: check success, remove callback if send is unsuccessfull
    callbacks.insert({rpc_id, cb});
  }

  return send_rpc(method, &params, rpc_id);
}

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   std::function<void(json&)> cb) {
  uint64_t rpc_id;
  {
    std::lock_guard<std::mutex> guard(cb_lock);
    rpc_id = id++;
    callbacks.insert({rpc_id, cb});
  }

  return send_rpc(method, nullptr, rpc_id);
}

int KWebSocketClient::send_jsonrpc(const std::string &method, const json &params, NotifyConsumer *consumer) {
  uint64_t rpc_id;
  {
    std::lock_guard<std::mutex> guard(cb_lock);
    rpc_id = id++;
    consumers.insert({rpc_id, consumer});
  }

  return send_rpc(method, &params, rpc_id);
}

void KWebSocketClient::register_notify_update(NotifyConsumer *consumer) {
  std::lock_guard<std::mutex> guard(cb_lock);
  if (std::find(notify_consumers.begin(), notify_consumers.end(), consumer) == std::end(notify_consumers)) {
    notify_consumers.push_back(consumer);
  }
}

void KWebSocketClient::unregister_notify_update(NotifyConsumer *consumer) {
  std::lock_guard<std::mutex> guard(cb_lock);
  // This was erase(remove_if(...)) with no end iterator. When the consumer was
  // not in the list remove_if returns end() and erasing that is undefined.
  notify_consumers.erase(std::remove_if(
    notify_consumers.begin(), notify_consumers.end(),
    [consumer](NotifyConsumer *c) {
      return c == consumer;
    }), notify_consumers.end());
}

int KWebSocketClient::send_rpc(const std::string &method, const json *params, uint64_t rpc_id) {
  json rpc;
  rpc["jsonrpc"] = "2.0";
  rpc["method"] = method;
  if (params != nullptr) {
    rpc["params"] = *params;
  }
  rpc["id"] = rpc_id;

  spdlog::debug("send_jsonrpc: {}", rpc.dump());
  return send(rpc.dump());
}

int KWebSocketClient::send_jsonrpc(const std::string &method,
				   const json &params) {
  return send_rpc(method, &params, id++);
}

int KWebSocketClient::send_jsonrpc(const std::string &method) {
  return send_rpc(method, nullptr, id++);
}

void KWebSocketClient::set_error_handler(std::function<void(const std::string&)> cb) {
  error_handler = cb;
}

int KWebSocketClient::gcode_script(const std::string &gcode) {
  json cmd = {{ "script", gcode }};

  // Check the reply. This used to be fire and forget, so a command Klipper
  // refused produced no log line and nothing on screen, which meant every
  // limit the UI enforces was advisory only. This runs on the libhv thread.
  return send_jsonrpc("printer.gcode.script", cmd, [this, gcode](json &reply) {
    if (!reply.contains("error")) {
      return;
    }

    std::string message = reply["/error/message"_json_pointer].is_string()
      ? reply["/error/message"_json_pointer].template get<std::string>()
      : reply["error"].dump();

    spdlog::error("klipper rejected gcode '{}': {}", gcode, message);
    if (error_handler) {
      error_handler(message);
    }
  });
}

void KWebSocketClient::register_method_callback(std::string resp_method,
						std::string handler_name,
						std::function<void(json&)> cb) {
  std::lock_guard<std::mutex> guard(cb_lock);

  const auto &entry = method_resp_cbs.find(resp_method);
  if (entry == method_resp_cbs.end()) {
    spdlog::debug("registering new method {}, handler {}", resp_method, handler_name);
    std::map<std::string, std::function<void(json&)>> handler_map;
    handler_map.insert({handler_name, cb});
    method_resp_cbs.insert({resp_method, handler_map});
  } else {
    spdlog::debug("found existing resp_method {} with handlers, updating handler callback {}",
		  resp_method, handler_name);
    entry->second.insert({handler_name, cb});
  }
}
