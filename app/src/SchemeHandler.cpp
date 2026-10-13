// SPDX-License-Identifier: MPL-2.0
#include "SchemeHandler.hpp"

#include <gio/gio.h>

#include <optional>
#include <string>

namespace urnw::scheme {

// Read only: a sandbox (Flatpak) answers for its own view of the desktop, and an
// answer other than this app's entry keeps the paste flow.
HandlerState HandlerStatus() {
  GAppInfo* app = g_app_info_get_default_for_uri_scheme(std::string(kScheme).c_str());
  std::optional<std::string> id;
  if (app) {
    if (const char* name = g_app_info_get_id(app)) id = name;
    g_object_unref(app);
  }
  return JudgeHandler(id);
}

}  // namespace urnw::scheme
