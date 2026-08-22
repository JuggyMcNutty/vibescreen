#ifndef __KTLS_H__
#define __KTLS_H__

namespace KTls {
  // Installs the process-wide libhv SSL context. Call once at startup, before
  // any libhv client exists, so that every wss:// and https:// connection
  // verifies the peer instead of falling back to an unverified one.
  void init();
}

#endif // __KTLS_H__
