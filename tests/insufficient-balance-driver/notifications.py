#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
#
# A local org.freedesktop.Notifications server on the driver container's
# session bus. With no org.gtk.Notifications owner and no portal, GLib's
# GNotification uses its freedesktop backend, so every
# Gio.Application.send_notification the GUI makes arrives here as Notify and
# every withdraw_notification as CloseNotification. Each call is appended as
# one JSON line to $URNETWORK_IB_NOTIFICATIONS; main.go counts the posts.
# Nothing is shown and nothing leaves the container.
import json
import os
import sys

from gi.repository import Gio, GLib

INTERFACE = """
<node>
  <interface name="org.freedesktop.Notifications">
    <method name="Notify">
      <arg type="s" direction="in"/>
      <arg type="u" direction="in"/>
      <arg type="s" direction="in"/>
      <arg type="s" direction="in"/>
      <arg type="s" direction="in"/>
      <arg type="as" direction="in"/>
      <arg type="a{sv}" direction="in"/>
      <arg type="i" direction="in"/>
      <arg type="u" direction="out"/>
    </method>
    <method name="CloseNotification">
      <arg type="u" direction="in"/>
    </method>
    <method name="GetCapabilities">
      <arg type="as" direction="out"/>
    </method>
    <method name="GetServerInformation">
      <arg type="s" direction="out"/>
      <arg type="s" direction="out"/>
      <arg type="s" direction="out"/>
      <arg type="s" direction="out"/>
    </method>
    <signal name="NotificationClosed">
      <arg type="u"/>
      <arg type="u"/>
    </signal>
    <signal name="ActionInvoked">
      <arg type="u"/>
      <arg type="s"/>
    </signal>
  </interface>
</node>
"""

log_path = os.environ["URNETWORK_IB_NOTIFICATIONS"]
next_id = [0]


def append(record):
    with open(log_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(record) + "\n")


def on_call(connection, sender, path, interface, method, params, invocation):
    if method == "Notify":
        app_name, replaces_id, _icon, summary, body, actions, _hints, _timeout = params.unpack()
        if replaces_id:
            notification_id = replaces_id
        else:
            next_id[0] += 1
            notification_id = next_id[0]
        append({"method": "Notify", "app_name": app_name, "replaces_id": replaces_id,
                "id": notification_id, "summary": summary, "body": body, "actions": list(actions)})
        invocation.return_value(GLib.Variant("(u)", (notification_id,)))
    elif method == "CloseNotification":
        append({"method": "CloseNotification", "id": params.unpack()[0]})
        invocation.return_value(None)
    elif method == "GetCapabilities":
        invocation.return_value(GLib.Variant("(as)", (["actions", "body"],)))
    elif method == "GetServerInformation":
        invocation.return_value(GLib.Variant("(ssss)", ("urnetwork-acceptance", "urnetwork", "1", "1.2")))
    else:
        invocation.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)


def main():
    node = Gio.DBusNodeInfo.new_for_xml(INTERFACE)
    loop = GLib.MainLoop()

    def on_bus(connection, _name):
        connection.register_object("/org/freedesktop/Notifications", node.interfaces[0], on_call, None, None)

    def on_lost(_connection, _name):
        print("could not own org.freedesktop.Notifications", file=sys.stderr)
        loop.quit()

    Gio.bus_own_name(Gio.BusType.SESSION, "org.freedesktop.Notifications",
                     Gio.BusNameOwnerFlags.NONE, on_bus, None, on_lost)
    loop.run()
    sys.exit(1)


if __name__ == "__main__":
    main()
