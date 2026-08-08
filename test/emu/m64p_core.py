"""Small ctypes frontend for the mupen64plus core library.

Loads the core directly (no console UI), attaches plugins, runs a ROM,
and lets a per-frame callback read emulated RAM through the debugger API.
The core must be built with DEBUGGER=1 for the DebugMemRead calls.
"""

import ctypes
import os

M64CMD_ROM_OPEN = 1
M64CMD_ROM_CLOSE = 2
M64CMD_EXECUTE = 5
M64CMD_STOP = 6
M64CMD_SET_FRAME_CALLBACK = 15
M64CMD_TAKE_NEXT_SCREENSHOT = 16

M64PLUGIN_RSP = 1
M64PLUGIN_GFX = 2
M64PLUGIN_AUDIO = 3
M64PLUGIN_INPUT = 4

CORE_API_VERSION = 0x020001

DEBUG_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p)
STATE_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_int)
FRAME_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint)


class Core:
    def __init__(self, install_dir, config_dir, verbose=False):
        self.install = install_dir
        self.verbose = verbose
        os.makedirs(config_dir, exist_ok=True)

        self.lib = ctypes.CDLL(os.path.join(install_dir, "lib", "libmupen64plus.so.2.0.0"))
        self.lib.DebugMemRead32.restype = ctypes.c_uint
        self.lib.DebugMemRead32.argtypes = [ctypes.c_uint]
        self.lib.DebugMemRead8.restype = ctypes.c_ubyte
        self.lib.DebugMemRead8.argtypes = [ctypes.c_uint]
        self.lib.DebugMemWrite32.restype = None
        self.lib.DebugMemWrite32.argtypes = [ctypes.c_uint, ctypes.c_uint]

        self._debug_cb = DEBUG_CB(self._on_debug)
        self._state_cb = STATE_CB(lambda ctx, p, v: None)
        rc = self.lib.CoreStartup(
            CORE_API_VERSION, config_dir.encode(),
            os.path.join(install_dir, "share", "mupen64plus").encode(),
            None, self._debug_cb, None, self._state_cb)
        if rc != 0:
            raise RuntimeError("CoreStartup failed: %d" % rc)
        self.plugins = []
        self._frame_cb = None

    def _on_debug(self, ctx, level, msg):
        if self.verbose and level <= 3:
            print("[core] %s" % msg.decode(errors="replace"))

    def load_rom(self, rom_path):
        with open(rom_path, "rb") as f:
            data = f.read()
        rc = self.lib.CoreDoCommand(M64CMD_ROM_OPEN, len(data), data)
        if rc != 0:
            raise RuntimeError("ROM_OPEN failed: %d" % rc)

    def attach_plugin(self, ptype, path):
        plug = ctypes.CDLL(path)
        rc = plug.PluginStartup(ctypes.c_void_p(self.lib._handle), None, self._debug_cb)
        if rc != 0:
            raise RuntimeError("PluginStartup(%s) failed: %d" % (path, rc))
        rc = self.lib.CoreAttachPlugin(ptype, ctypes.c_void_p(plug._handle))
        if rc != 0:
            raise RuntimeError("CoreAttachPlugin(%s) failed: %d" % (path, rc))
        self.plugins.append((ptype, plug))

    def attach_standard_plugins(self, input_plugin_path):
        plugdir = os.path.join(self.install, "lib", "mupen64plus")
        self.attach_plugin(M64PLUGIN_GFX, os.path.join(plugdir, "mupen64plus-video-rice.so"))
        self.attach_plugin(M64PLUGIN_AUDIO, os.path.join(plugdir, "mupen64plus-audio-sdl.so"))
        self.attach_plugin(M64PLUGIN_INPUT, input_plugin_path)
        self.attach_plugin(M64PLUGIN_RSP, os.path.join(plugdir, "mupen64plus-rsp-hle.so"))

    def run(self, frame_callback):
        """Runs the ROM. frame_callback(frame) may call stop()."""
        self._frame_cb = FRAME_CB(frame_callback)
        self.lib.CoreDoCommand(M64CMD_SET_FRAME_CALLBACK, 0, self._frame_cb)
        rc = self.lib.CoreDoCommand(M64CMD_EXECUTE, 0, None)
        if rc != 0:
            raise RuntimeError("EXECUTE failed: %d" % rc)

    def stop(self):
        self.lib.CoreDoCommand(M64CMD_STOP, 0, None)

    def take_screenshot(self):
        """Captures the next rendered frame as a PNG under
        $XDG_DATA_HOME/mupen64plus/screenshot/ (set XDG_DATA_HOME before
        constructing the Core to control where they land)."""
        self.lib.CoreDoCommand(M64CMD_TAKE_NEXT_SCREENSHOT, 0, None)

    def read_u32(self, addr):
        return self.lib.DebugMemRead32(ctypes.c_uint(addr))

    def write_u32(self, addr, value):
        self.lib.DebugMemWrite32(ctypes.c_uint(addr), ctypes.c_uint(value & 0xFFFFFFFF))

    def read_s32(self, addr):
        v = self.read_u32(addr)
        return v - 0x100000000 if v >= 0x80000000 else v

    def read_bytes(self, addr, length):
        return bytes(self.lib.DebugMemRead8(ctypes.c_uint(addr + i)) for i in range(length))

    def read_cstring(self, addr, limit=64):
        out = bytearray()
        for i in range(limit):
            b = self.lib.DebugMemRead8(ctypes.c_uint(addr + i))
            if b == 0:
                break
            out.append(b)
        return bytes(out)

    def shutdown(self):
        self.lib.CoreDoCommand(M64CMD_ROM_CLOSE, 0, None)
        for ptype, plug in self.plugins:
            self.lib.CoreDetachPlugin(ptype)
            plug.PluginShutdown()
        self.lib.CoreShutdown()


def load_map_symbols(map_path, names):
    """Reads RAM addresses for the named globals from the linker map."""
    wanted = set(names)
    out = {}
    with open(map_path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2 and parts[1] in wanted and parts[0].startswith("0x"):
                out[parts[1]] = int(parts[0], 16)
    missing = wanted - set(out)
    if missing:
        raise RuntimeError("symbols not in map: %s" % ", ".join(sorted(missing)))
    return out
