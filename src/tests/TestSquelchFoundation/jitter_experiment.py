#!/usr/bin/env python3
# Copyright The Mumble-Funk Contributors. All rights reserved.
# BSD-style license: see LICENSE.
"""Local Speex jitter-buffer counterexamples, not a real-client listening test.

Optional research probe: Python stdlib + the existing libspeexdsp used by Mumble.
No network, files, server configuration or third-party code changes.
"""
import ctypes as c
import ctypes.util


class Packet(c.Structure):
    _fields_ = [('data', c.c_void_p), ('len', c.c_uint32),
                ('timestamp', c.c_uint32), ('span', c.c_uint32),
                ('sequence', c.c_uint16), ('user_data', c.c_uint32)]


def main():
    name = ctypes.util.find_library('speexdsp')
    if not name:
        raise SystemExit('NOT RUN: libspeexdsp is needed for this optional research probe')
    lib = c.CDLL(name)
    lib.jitter_buffer_init.argtypes = [c.c_int]
    lib.jitter_buffer_init.restype = c.c_void_p
    lib.jitter_buffer_destroy.argtypes = [c.c_void_p]
    lib.jitter_buffer_put.argtypes = [c.c_void_p, c.POINTER(Packet)]
    lib.jitter_buffer_get.argtypes = [c.c_void_p, c.POINTER(Packet), c.c_int, c.POINTER(c.c_int)]
    lib.jitter_buffer_tick.argtypes = [c.c_void_p]
    buffers = []

    def make():
        buffer = lib.jitter_buffer_init(480)  # One 10-ms mono sequence unit.
        if not buffer:
            raise RuntimeError('jitter buffer allocation failed')
        buffers.append(buffer)
        return buffer

    def put(buffer, label, timestamp):
        data = c.create_string_buffer(label)
        packet = Packet(c.cast(data, c.c_void_p), len(label), timestamp, 960, 0, 0)
        lib.jitter_buffer_put(buffer, c.byref(packet))  # Default API copies the data.

    def get(buffer):
        data = c.create_string_buffer(64)
        packet = Packet(c.cast(data, c.c_void_p), len(data), 0, 0, 0, 0)
        offset = c.c_int()
        status = lib.jitter_buffer_get(buffer, c.byref(packet), 480, c.byref(offset))
        return status, bytes(data[:packet.len]), packet.timestamp

    def advance(buffer):
        lib.jitter_buffer_tick(buffer)
        lib.jitter_buffer_tick(buffer)  # Consumed a 20-ms packet.

    try:
        one = make()
        put(one, b'voice', 960)
        put(one, b'open', 0)
        assert get(one)[:2] == (0, b'open')
        advance(one)
        assert get(one)[:2] == (0, b'voice')
        print('PASS: one buffer orders queued packets by timestamp before playback')

        tone, speech = make(), make()
        put(tone, b'open', 0)
        put(speech, b'voice', 48000)
        assert get(tone)[:2] == (0, b'open')
        assert get(speech)[:2] == (0, b'voice')
        print('PASS: independent streams both start now; a larger frame number is not a cross-stream delay')

        late = make()
        put(late, b'voice', 1920)
        assert get(late)[:2] == (0, b'voice')
        advance(late)
        put(late, b'open', 0)
        status, payload, _ = get(late)
        assert status != 0 or payload != b'open'
        print('PASS: a prefix arriving after voice playout cannot retroactively play first')
    finally:
        for buffer in buffers:
            lib.jitter_buffer_destroy(buffer)


if __name__ == '__main__':
    main()
