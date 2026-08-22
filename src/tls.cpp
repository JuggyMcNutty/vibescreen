#include "tls.h"

#include "config.h"

#include "hv/hssl.h"

#include "spdlog/spdlog.h"

#include <cstring>
#include <experimental/filesystem>
#include <string>
#include <vector>

namespace fs = std::experimental::filesystem;

namespace {

  // Searched in order. The first two are where a distribution puts its trust
  // store, so the simulator and the Debian package find one without being
  // told. A K1 has none of them, which is why the directory beside the binary
  // is searched first: dropping a bundle into /usr/data/guppyscreen is then
  // the whole of the setup.
  std::vector<std::string> candidates() {
    std::vector<std::string> paths;

    Config *conf = Config::get_instance();
    const json &configured = conf->get_json("/ca_file");
    if (configured.is_string()) {
      paths.push_back(configured.template get<std::string>());
    }

    try {
      paths.push_back((fs::canonical("/proc/self/exe").parent_path() / "cacert.pem").string());
    } catch (const std::exception &e) {
      spdlog::debug("cannot locate the binary's own directory: {}", e.what());
    }

    paths.push_back("/etc/ssl/certs/ca-certificates.crt");  // Debian, Ubuntu, Alpine
    paths.push_back("/etc/pki/tls/certs/ca-bundle.crt");    // Fedora, RHEL
    paths.push_back("/etc/ssl/cert.pem");                   // BSD, some musl images

    return paths;
  }

}

namespace KTls {

  void init() {
    std::string ca_file;
    const std::vector<std::string> paths = candidates();

    for (const auto &path : paths) {
      try {
        if (fs::is_regular_file(path)) {
          ca_file = path;
          break;
        }
      } catch (const std::exception &e) {
        spdlog::debug("cannot stat {}: {}", path, e.what());
      }
    }

    hssl_ctx_init_param_t param;
    memset(&param, 0, sizeof(param));
    param.endpoint = HSSL_CLIENT;

    // Always 1, including when nothing was found. libhv resolves a client's
    // context as io->ssl_ctx, then g_ssl_ctx, then a fresh one built from no
    // options at all, and that last one verifies nothing. So the choice here is
    // not "verify or not", it is "fail closed or connect to anyone claiming to
    // be the printer". Leaving g_ssl_ctx unset would take the third branch.
    param.verify_peer = 1;
    if (!ca_file.empty()) {
      param.ca_file = ca_file.c_str();
    }

    if (hssl_ctx_init(&param) == NULL) {
      spdlog::error("could not initialise TLS, https and wss urls will not connect");
      return;
    }

    if (ca_file.empty()) {
      spdlog::warn("no CA bundle found, so https and wss urls will not connect");
      for (const auto &path : paths) {
        spdlog::warn("  tried {}", path);
      }
      spdlog::warn("  set \"ca_file\" in guppyconfig.json, or put a bundle at one of those paths");
      return;
    }

    spdlog::info("TLS backend {}, verifying against {}", hssl_backend(), ca_file);
  }

}
