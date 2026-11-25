#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Minimal org.freedesktop.Notifications server for GUI tests on a private D-Bus
# session: logs every Notify call (app, summary, body) as one line to the file
# given as argument. Never run on the user's session bus.
import sys

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

LOG = sys.argv[1]


class Server(dbus.service.Object):
    def __init__(self, bus):
        name = dbus.service.BusName("org.freedesktop.Notifications", bus)
        super().__init__(name, "/org/freedesktop/Notifications")
        self.next_id = 1

    @dbus.service.method("org.freedesktop.Notifications", in_signature="", out_signature="as")
    def GetCapabilities(self):
        return ["body", "actions", "icon-static"]

    @dbus.service.method("org.freedesktop.Notifications", in_signature="", out_signature="ssss")
    def GetServerInformation(self):
        return ("fake-notifyd", "panero-tests", "1.0", "1.2")

    @dbus.service.method("org.freedesktop.Notifications", in_signature="susssasa{sv}i", out_signature="u")
    def Notify(self, app, replaces, icon, summary, body, actions, hints, timeout):
        with open(LOG, "a", encoding="utf-8") as f:
            f.write("%s | %s | %s\n" % (app, summary, body.replace("\n", " / ")))
        self.next_id += 1
        return self.next_id

    @dbus.service.method("org.freedesktop.Notifications", in_signature="u", out_signature="")
    def CloseNotification(self, nid):
        pass


DBusGMainLoop(set_as_default=True)
if not dbus.SessionBus().get_unique_name():
    sys.exit(1)
Server(dbus.SessionBus())
GLib.MainLoop().run()
