#!/usr/bin/env python
# Copyright 2011 Christoph Reiter <christoph.reiter@gmx.at>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.

import os
import sys

env_key = "GST_PLUGIN_PATH"
build_path = os.path.join(
    os.path.dirname(os.path.realpath(sys.argv[0])), "src", ".libs")
if not env_key in os.environ:
    print "Info: '%s' not set, using default: '%s'" % (env_key, build_path)
    so_path = os.path.join(build_path, "libgstbs2b.so")
    if not os.path.exists(so_path):
        print "Warning: Could not find '%s'. Did you type 'make'?" % so_path
        sys.exit(1)
    os.environ[env_key] = build_path

import unittest
import threading

import gst
import gobject

gobject.threads_init()

# These are missing in gobject
gobject.PARAM_STATIC_NAME = 1 << 5
gobject.PARAM_STATIC_NICK = 1 << 6
gobject.PARAM_STATIC_BLURB = 1 << 7
gobject.PARAM_STATIC_STRINGS = (
    gobject.PARAM_STATIC_NAME |
    gobject.PARAM_STATIC_NICK |
    gobject.PARAM_STATIC_BLURB)

class CrossfeedProperties(unittest.TestCase):
    def setUp(self):
        self.crossfeed = gst.element_factory_make('crossfeed')
        self.props = gobject.list_properties(self.crossfeed)
        self.props = dict(((p.name, p) for p in self.props))

    def test_names_nicks(self):
        for name in ["fcut", "feed", "preset", "active"]:
            self.assertTrue(name in self.props)

        nicks = [p.nick for p in self.props.values()]
        for name in ["Frequency cut", "Feed level", "Preset", "Active"]:
            self.assertTrue(name in nicks)

        for i, nick in enumerate(["default", "cmoy", "jmeier"]):
            self.crossfeed.set_property("preset", i)
            self.assertEqual(
                self.crossfeed.get_property("preset").value_nick, nick)

        for i, name in enumerate(["speaker", "Chu Moy", "Jan Meier"]):
            self.crossfeed.set_property("preset", i)
            self.assertTrue(name in
                self.crossfeed.get_property("preset").value_name)

        self.crossfeed.set_property("fcut", 999)
        self.assertEqual(
            self.crossfeed.get_property("preset").value_nick, "none")
        self.assertTrue("No " in
            self.crossfeed.get_property("preset").value_name)

    def test_default(self):
        self.assertEqual(self.props["active"].default_value, True)
        self.assertEqual(self.props["fcut"].default_value, 700)
        self.assertEqual(self.props["feed"].default_value, 4.5)
        self.assertEqual(self.props["preset"].default_value, 0)
        self.assertEqual(
            self.props["preset"].default_value.value_nick, "default")

    def test_real_default(self):
        self.assertEqual(self.crossfeed.get_property("active"), True)
        self.assertEqual(self.crossfeed.get_property("fcut"), 700)
        self.assertEqual(self.crossfeed.get_property("feed"), 4.5)
        self.assertEqual(self.crossfeed.get_property("preset").real, 0)

    def test_range(self):
        self.assertEqual(self.props["fcut"].maximum, 2000)
        self.assertEqual(self.props["fcut"].minimum, 300)

        self.assertEqual(self.props["feed"].maximum, 15.0)
        self.assertEqual(self.props["feed"].minimum, 1.0)

    def test_types(self):
        self.assertEqual(self.props["active"].value_type, gobject.TYPE_BOOLEAN)
        self.assertEqual(self.props["fcut"].value_type, gobject.TYPE_INT)
        self.assertEqual(self.props["feed"].value_type, gobject.TYPE_FLOAT)
        self.assertEqual(
            self.props["preset"].value_type.parent, gobject.TYPE_ENUM)

    def test_flags(self):
        for name in ["active", "feed", "fcut", "preset"]:
            self.assertEqual(self.props[name].flags,
                gobject.PARAM_READWRITE |
                gobject.PARAM_STATIC_STRINGS |
                gst.PARAM_CONTROLLABLE)

    def dearDown(self):
        del self.crossfeed

class CrossfeedCaps(unittest.TestCase):
    def setUp(self):
        self.mainloop = gobject.MainLoop()
        self.p = None

    def __stop(self, error):
        if self.p:
            self.p.set_state(gst.STATE_NULL)
            bus = self.p.get_bus()
            bus.disconnect(self.bus_id)
            bus.remove_signal_watch()
            self.p = None
        self.error = error
        self.mainloop.quit()

    def __start(self, t):
        self.error = False
        try: self.p = gst.parse_launch("audiotestsrc num-buffers=42 ! " + t)
        except gobject.GError:
            self.__stop(True)
            return

        bus = self.p.get_bus()
        bus.add_signal_watch()
        bus.enable_sync_message_emission()
        self.bus_id = bus.connect("sync-message", self.__message)

        self.p.set_state(gst.STATE_PLAYING)

    def __message(self, bus, message):
        if message.type == gst.MESSAGE_EOS:
            gobject.idle_add(self.__stop, False)
        elif message.type == gst.MESSAGE_ERROR:
            gobject.idle_add(self.__stop, True)

    def __check(self, t):
        gobject.idle_add(self.__start, t)
        self.mainloop.run()
        return not self.error

    def test_basic(self):
        r = self.__check("crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,width=8,depth=8 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

    def test_formats(self):
        for width in [8, 16, 24, 32]:
            r = self.__check(
                "audioconvert ! audio/x-raw-int,width=%d,depth=%d ! "
                "crossfeed ! fakesink" % (width, width))
            self.assertTrue(r)

        for width in [32, 64]:
            r = self.__check(
                "audioconvert ! audio/x-raw-float,width=%d,depth=%d ! "
                "crossfeed ! fakesink" % (width, width))
            self.assertTrue(r)

        for depth in range(1, 65, 7):
            r = self.__check(
                "audioconvert ! audio/x-raw-float,width=%d,depth=%d ! "
                "crossfeed ! fakesink" % (64, depth))
            self.assertTrue(r)

        for depth in [8, 16, 24, 32]:
            r = self.__check(
                "audioconvert ! audio/x-raw-int,width=%d,depth=%d ! "
                "crossfeed ! fakesink" % (32, depth))
            self.assertTrue(r)

    def test_channels(self):
        r = self.__check(
            "audioconvert ! audio/x-raw-int,channels=1 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,channels=2 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,channels=3 ! "
            "crossfeed ! fakesink")
        self.assertFalse(r)

    def test_endianness(self):
        r = self.__check(
            "audioconvert ! audio/x-raw-int,endianess=1234 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,endianess=4321 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

    def test_rate(self):
        r = self.__check(
            "audioconvert ! audio/x-raw-int,rate=2000 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,rate=384000 ! "
            "crossfeed ! fakesink")
        self.assertTrue(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,rate=1999 ! "
            "crossfeed ! fakesink")
        self.assertFalse(r)

        r = self.__check(
            "audioconvert ! audio/x-raw-int,rate=384001 ! "
            "crossfeed ! fakesink")
        self.assertFalse(r)

if __name__ == "__main__":
    unittest.main()
