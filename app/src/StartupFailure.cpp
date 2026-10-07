// SPDX-License-Identifier: MPL-2.0
#include "StartupFailure.hpp"

#include <adwaita.h>
#include <gtkmm/application.h>

#include "I18n.hpp"

namespace urnw::startup_failure {

bool Show(Gtk::Application& app, const std::string& error, const std::string& logDir) {
  if (gdk_display_get_default() == nullptr) return false;
  adw_init();  // the startup that failed never reached its own
  const std::string body =
      Body(Format(T_("app_start_failed_detail", "URnetwork could not start. Its logs are in {}."),
                  logDir),
           error);
  GtkWidget* dialog = adw_message_dialog_new(
      nullptr, T_("something_went_wrong", "Something went wrong."), body.c_str());
  // the product name, never translated, for the shell's window list
  gtk_window_set_title(GTK_WINDOW(dialog), "URnetwork");
  adw_message_dialog_add_response(ADW_MESSAGE_DIALOG(dialog), "close", T_("close", "Close"));
  adw_message_dialog_set_default_response(ADW_MESSAGE_DIALOG(dialog), "close");
  adw_message_dialog_set_close_response(ADW_MESSAGE_DIALOG(dialog), "close");
  // a window of the app's keeps it running until the dialog is closed
  gtk_window_set_application(GTK_WINDOW(dialog), app.gobj());
  g_signal_connect(dialog, "response",
                   G_CALLBACK(+[](AdwMessageDialog*, const char*, gpointer application) {
                     g_application_quit(G_APPLICATION(application));
                   }),
                   app.gobj());
  gtk_window_present(GTK_WINDOW(dialog));
  return true;
}

}  // namespace urnw::startup_failure
