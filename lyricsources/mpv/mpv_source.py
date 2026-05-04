# -*- coding: utf-8 -*-
#
# Copyright (C) 2025 OSD Lyrics Contributors
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

import json
import logging
import os
import socket
import sys

from osdlyrics.lyricsource import BaseLyricSourcePlugin, SearchResult

MPV_SOURCE_ID = 'mpv'
MPV_SOURCE_NAME = 'mpv (embedded lyrics)'

# The socket path is read once at import time from --socket=<path> argument.
_socket_path = os.environ.get('OSDLYRICS_MPV_SOCKET')
_filtered_argv = [sys.argv[0]]
for _arg in sys.argv[1:]:
    if _arg.startswith('--socket='):
        _socket_path = _arg[len('--socket='):]
        continue
    _filtered_argv.append(_arg)
# Base App uses optparse and rejects unknown args. Strip plugin-specific args first.
sys.argv = _filtered_argv


def _query_mpv(sock_path, prop):
    """Send a get_property command to the mpv IPC socket and return the data.

    Returns the value of the property on success, None if the property is
    unavailable or not found, or raises an exception for other errors.
    """
    cmd = json.dumps({'command': ['get_property', prop]}) + '\n'
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.settimeout(5)
        s.connect(sock_path)
        s.sendall(cmd.encode('utf-8'))
        # mpv responds with one JSON object per line
        buf = b''
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
            if b'\n' in buf:
                break
    line = buf.split(b'\n')[0]
    response = json.loads(line.decode('utf-8'))
    error = response.get('error')
    if error in ('property unavailable', 'property not found'):
        logging.info('mpv: property "%s" not available', prop)
        return None
    if error != 'success':
        raise RuntimeError('mpv IPC error: %s' % error)
    return response.get('data')


class MpvLyricSource(BaseLyricSourcePlugin):
    """Lyric source that fetches embedded lyrics directly from mpv via its IPC socket.

    The mpv socket path must be supplied as a command-line argument when
    starting this plugin:

        mpv_source.py --socket=/tmp/mpv.sock
    """

    def __init__(self):
        super().__init__(id=MPV_SOURCE_ID, name=MPV_SOURCE_NAME)

    def do_search(self, metadata):
        if not _socket_path:
            logging.error('mpv lyric source: no --socket argument provided')
            return []

        try:
            lyrics = _query_mpv(_socket_path, 'metadata/lyrics')
        except Exception as e:
            logging.warning('mpv lyric source: failed to query socket %s: %s',
                            _socket_path, e)
            return []

        if not lyrics:
            return []

        # Return a single candidate; the download step will re-fetch the content.
        return [
            SearchResult(
                title=metadata.title or '',
                artist=metadata.artist or '',
                album=metadata.album or '',
                sourceid=MPV_SOURCE_ID,
                downloadinfo=_socket_path,
            )
        ]

    def do_download(self, downloadinfo):
        sock_path = str(downloadinfo)
        try:
            lyrics = _query_mpv(sock_path, 'metadata/lyrics')
        except Exception as e:
            raise RuntimeError(
                'mpv lyric source: failed to download lyrics from %s: %s' %
                (sock_path, e)
            ) from e

        if not lyrics:
            raise RuntimeError(
                'mpv lyric source: no lyrics in metadata/lyrics from %s' % sock_path
            )

        return lyrics.encode('utf-8')


if __name__ == '__main__':
    source = MpvLyricSource()
    source.app.run()
