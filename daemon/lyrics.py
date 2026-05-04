# -*- coding: utf-8 -*-
#
# Copyright (C) 2011  Tiger Soldier
#
# This file is part of OSD Lyrics.
#
# OSD Lyrics is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# OSD Lyrics is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with OSD Lyrics.  If not, see <https://www.gnu.org/licenses/>.
#

import logging

import dbus
import dbus.service

import osdlyrics
from osdlyrics.app import App
import osdlyrics.lrc
from osdlyrics.metadata import Metadata

LYRICS_INTERFACE = 'org.osdlyrics.Lyrics'
LYRICS_OBJECT_PATH = '/org/osdlyrics/Lyrics'

# URI used to represent lyrics held in memory (not backed by any file).
MPV_LYRICS_URI = 'mpv:'


def metadata_description(metadata):
    if metadata.title is None:
        return '[Unknown]'
    if metadata.artist is None:
        return metadata.title
    return '%s(%s)' % (metadata.title, metadata.artist)


def _metadata_key(metadata):
    """Return a hashable key that identifies a track."""
    return (metadata.title or '', metadata.artist or '', metadata.location or '')


class LyricsService(dbus.service.Object):

    def __init__(self, conn):
        super().__init__(conn=conn, object_path=LYRICS_OBJECT_PATH)
        self._metadata = Metadata()
        # In-memory store: metadata_key -> lrc content (str)
        self._lyrics_cache = {}

    def assign_lrc_uri(self, metadata, uri):
        # No-op: file-based assignment is no longer supported.
        logging.info('assign_lrc_uri called but file-based lyrics are disabled')

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='a{sv}',
                         out_signature='bsa{ss}aa{sv}')
    def GetLyrics(self, metadata):
        ret, uri, content = self.GetRawLyrics(metadata)
        if ret:
            attr, lines = osdlyrics.lrc.parse_lrc(content)
            return ret, uri, attr, lines
        else:
            return ret, uri, {}, []

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='a{sv}',
                         out_signature='bss')
    def GetRawLyrics(self, metadata):
        if isinstance(metadata, dict):
            metadata = Metadata.from_dict(metadata)
        key = _metadata_key(metadata)
        content = self._lyrics_cache.get(key)
        if content is not None:
            logging.info('LRC for track %s found in cache', metadata_description(metadata))
            return True, MPV_LYRICS_URI, content
        logging.info('LRC for track %s not found', metadata_description(metadata))
        return False, '', ''

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='',
                         out_signature='bsa{ss}aa{sv}')
    def GetCurrentLyrics(self):
        return self.GetLyrics(self._metadata)

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='',
                         out_signature='bss')
    def GetCurrentRawLyrics(self):
        return self.GetRawLyrics(self._metadata)

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='a{sv}ay',
                         out_signature='s',
                         byte_arrays=True)
    def SetLyricContent(self, metadata, content):
        metadata = Metadata.from_dict(metadata)
        key = _metadata_key(metadata)
        self._lyrics_cache[key] = content.rstrip(b'\0').decode('utf-8', errors='replace')
        logging.info('Stored LRC for track %s in cache', metadata_description(metadata))
        if metadata == self._metadata:
            self.CurrentLyricsChanged()
        return MPV_LYRICS_URI

    @dbus.service.method(dbus_interface=LYRICS_INTERFACE,
                         in_signature='a{sv}s',
                         out_signature='')
    def AssignLyricFile(self, metadata, uri):
        # File-based assignment is no longer supported; kept for API compatibility.
        logging.info('AssignLyricFile called but file-based lyrics are disabled')

    @dbus.service.signal(dbus_interface=LYRICS_INTERFACE,
                         signature='')
    def CurrentLyricsChanged(self):
        pass

    def set_current_metadata(self, metadata):
        logging.info('Setting current metadata: %s', metadata)
        self._metadata = metadata


def test():
    app = App('Lyrics', False)
    lyrics_service = LyricsService(app.connection)  # noqa: F841
    app.run()


if __name__ == '__main__':
    test()

