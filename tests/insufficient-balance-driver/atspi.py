#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
#
# The GUI-facing half of the Linux insufficient-balance driver: a thin AT-SPI
# client run as the GUI user inside its session (container.sh atspi ...).
# It reports and acts; main.go decides.
#
#   tree                       the URnetwork application's accessibility tree
#                              as JSON (role, name, states, attributes,
#                              actions, child-index path)
#   action PATH NAME           invoke the named AT-SPI action ("click",
#                              "toggle") on the node at PATH
#   focus PATH                 click the centre of the node at PATH (GTK 4.14
#                              does not implement Component.GrabFocus)
#   type-credential KEY        type the email or password from the private
#                              credentials copy into the focused field; the
#                              value is never printed
#   wait-app SECONDS           wait for the GUI to register on the a11y bus
#   has-name NAME              exit 0 when NAME is owned on the session bus
#
# PATH is a comma-separated child-index path from the application root.
import json
import os
import subprocess
import sys
import time

import gi

gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, Gio, GLib  # noqa: E402

MAX_NODES = 20000
MAX_DEPTH = 80
APP_MARKERS = ("urnetwork", "com.bringyour.network")

STATE_NAMES = {
    Atspi.StateType.SHOWING: "showing",
    Atspi.StateType.VISIBLE: "visible",
    Atspi.StateType.SENSITIVE: "sensitive",
    Atspi.StateType.ENABLED: "enabled",
    Atspi.StateType.CHECKED: "checked",
    Atspi.StateType.PRESSED: "pressed",
    Atspi.StateType.FOCUSED: "focused",
}


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def find_app():
    desktop = Atspi.get_desktop(0)
    names = []
    for i in range(desktop.get_child_count()):
        app = desktop.get_child_at_index(i)
        if app is None:
            continue
        name = app.get_name() or ""
        names.append(name)
        if any(marker in name.lower() for marker in APP_MARKERS):
            return app
    return None


def require_app():
    app = find_app()
    if app is None:
        fail("the URnetwork GUI is not on the accessibility bus")
    return app


def node_json(acc, path, budget, depth):
    budget[0] -= 1
    states = []
    state_set = acc.get_state_set()
    for state, name in STATE_NAMES.items():
        if state_set.contains(state):
            states.append(name)
    try:
        attributes = acc.get_attributes() or {}
    except GLib.Error:
        attributes = {}
    actions = []
    action = acc.get_action_iface()
    if action is not None:
        for i in range(action.get_n_actions()):
            actions.append(action.get_action_name(i))
    children = []
    if depth < MAX_DEPTH:
        for i in range(acc.get_child_count()):
            if budget[0] <= 0:
                break
            child = acc.get_child_at_index(i)
            if child is not None:
                children.append(node_json(child, path + [i], budget, depth + 1))
    return {
        "role": acc.get_role_name() or "",
        "name": acc.get_name() or "",
        "states": states,
        "attributes": {str(k): str(v) for k, v in attributes.items()},
        "actions": actions,
        "path": path,
        "children": children,
    }


def resolve(path_arg):
    acc = require_app()
    if path_arg:
        for part in path_arg.split(","):
            acc = acc.get_child_at_index(int(part))
            if acc is None:
                fail("the accessible at that path is gone")
    return acc


def do_action(path_arg, name):
    acc = resolve(path_arg)
    action = acc.get_action_iface()
    if action is None:
        fail("the accessible has no actions")
    for i in range(action.get_n_actions()):
        if action.get_action_name(i) == name:
            if not action.do_action(i):
                fail("the action was refused")
            return
    fail("the accessible has no %s action" % name)


def window_origin():
    # no window manager: the GUI's toplevel is the only mapped URnetwork window
    out = subprocess.run(
        ["xdotool", "search", "--onlyvisible", "--name", "URnetwork", "getwindowgeometry", "--shell"],
        capture_output=True, text=True, timeout=10)
    x = y = 0
    for line in out.stdout.splitlines():
        key, _, value = line.partition("=")
        if key == "X":
            x = int(value)
        elif key == "Y":
            y = int(value)
    return x, y


def focus(path_arg):
    acc = resolve(path_arg)
    component = acc.get_component_iface()
    if component is None:
        fail("the accessible has no extents")
    extents = component.get_extents(Atspi.CoordType.WINDOW)
    ox, oy = window_origin()
    x = ox + extents.x + max(1, extents.width // 2)
    y = oy + extents.y + max(1, extents.height // 2)
    subprocess.run(["xdotool", "mousemove", "--sync", str(x), str(y), "click", "1"],
                   check=True, timeout=10, capture_output=True)
    # select whatever the field holds so typing replaces it
    subprocess.run(["xdotool", "key", "--clearmodifiers", "ctrl+a"], check=True, timeout=10, capture_output=True)


def read_credential(key):
    path = os.environ.get("URNETWORK_IB_CREDENTIALS", "")
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                k, sep, v = line.partition(":")
                if sep and k.strip() == key:
                    v = v.strip()
                    if len(v) >= 2 and v[0] in "\"'" and v[-1] == v[0]:
                        v = v[1:-1]
                    return v
    except OSError:
        pass
    fail("the private credentials copy has no %s" % key)


def type_credential(key):
    if key not in ("email", "password"):
        fail("type-credential takes email or password")
    value = read_credential(key)
    # XTest through the AT-SPI registry: the value stays in this process
    if not Atspi.generate_keyboard_event(0, value, Atspi.KeySynthType.STRING):
        fail("typing was refused")


def wait_app(seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            if find_app() is not None:
                return
        except GLib.Error:
            pass
        time.sleep(1)
    fail("the URnetwork GUI did not appear on the accessibility bus within %ds" % seconds)


def has_name(name):
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    reply = bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                          "NameHasOwner", GLib.Variant("(s)", (name,)), GLib.VariantType("(b)"),
                          Gio.DBusCallFlags.NONE, 5000, None)
    sys.exit(0 if reply.unpack()[0] else 1)


def main(argv):
    if not argv:
        fail("usage: atspi.py tree|action|focus|type-credential|wait-app|has-name")
    verb, args = argv[0], argv[1:]
    if verb == "has-name" and len(args) == 1:
        has_name(args[0])
    Atspi.init()
    if verb == "tree" and not args:
        json.dump(node_json(require_app(), [], [MAX_NODES], 0), sys.stdout)
        sys.stdout.write("\n")
    elif verb == "action" and len(args) == 2:
        do_action(args[0], args[1])
    elif verb == "focus" and len(args) == 1:
        focus(args[0])
    elif verb == "type-credential" and len(args) == 1:
        type_credential(args[0])
    elif verb == "wait-app" and len(args) == 1:
        wait_app(int(args[0]))
    else:
        fail("unknown atspi.py verb or arguments")


if __name__ == "__main__":
    main(sys.argv[1:])
