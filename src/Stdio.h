#pragma once

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace rpcpp {

// Switches stdin/stdout to binary mode on Windows so binary codecs don't
// get mangled by CRLF translation. No-op elsewhere. Invoked automatically
// by RpcServer::run() when the codec advertises is_binary == true; users
// driving processMessage themselves should call this once if they pipe
// binary frames through standard I/O.
inline void set_binary_stdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

}  // namespace rpcpp
