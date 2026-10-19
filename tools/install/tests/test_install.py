"""Tests for the installer's disc and icon code. They build a tiny XDVDFS image
and a tiny XBE in memory: no game data is needed.

    python3 -m unittest discover -s tools/install/tests
"""

import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

import bundle    # noqa: E402
import disc      # noqa: E402
import linux     # noqa: E402
import steam     # noqa: E402
import win       # noqa: E402
import regen     # noqa: E402
import common    # noqa: E402
import icons     # noqa: E402
import xbe       # noqa: E402
import xdvdfs    # noqa: E402

S = xdvdfs.SECTOR


def build_image(files):
    """The game partition of an XDVDFS image of {path: bytes}."""
    dirs = {"": []}                                    # dir path -> [(name, is_dir)]
    for p in files:
        parts = p.split("/")
        for i in range(len(parts)):
            d = "/".join(parts[:i])
            e = (parts[i], i < len(parts) - 1)
            dirs.setdefault(d, [])
            if e not in dirs[d]:
                dirs[d].append(e)
            if e[1]:
                dirs.setdefault("/".join(parts[:i + 1]), [])
    nxt = [33]                                         # sector 32 is the descriptor
    layout = {}                                        # path -> (sector, size)
    data = {}                                          # sector -> bytes

    def alloc(blob):
        sec = nxt[0]
        nxt[0] += (len(blob) + S - 1) // S or 1
        data[sec] = blob
        return sec

    def table(d):
        entries = sorted(dirs[d], key=lambda e: e[0].lower())
        recs = []
        for name, is_dir in entries:
            path = d + "/" + name if d else name
            if is_dir:
                sec, size = layout[path]
            else:
                sec, size = layout[path]
            recs.append((name, is_dir, sec, size))
        # balanced BST laid out in a flat table, root first (offset 0), as the
        # real format has it
        def rec_len(name):
            return (14 + len(name) + 3) // 4 * 4
        order = []

        def preorder(lo, hi):
            if lo >= hi:
                return
            mid = (lo + hi) // 2
            order.append(mid)
            preorder(lo, mid)
            preorder(mid + 1, hi)
        preorder(0, len(recs))
        offs, pos = {}, 0
        for i in order:
            offs[i] = pos // 4
            pos += rec_len(recs[i][0])
        raw = bytearray(b"\xff" * max((pos + S - 1) // S * S, S))

        def build(lo, hi):
            if lo >= hi:
                return 0
            mid = (lo + hi) // 2
            name, is_dir, sec, size = recs[mid]
            left, right = build(lo, mid), build(mid + 1, hi)
            blob = (struct.pack("<HHIIBB", left, right, sec, size,
                                0x10 if is_dir else 0x20, len(name)) + name.encode("latin-1"))
            raw[offs[mid] * 4:offs[mid] * 4 + len(blob)] = blob
            return offs[mid]
        build(0, len(recs))
        return bytes(raw)

    # files first, then directories deepest first (a dir's table needs its children's sectors)
    for p, blob in files.items():
        layout[p] = (alloc(blob), len(blob))
    for d in sorted((d for d in dirs if d), key=lambda x: -x.count("/")):
        t = table(d)
        layout[d] = (alloc(t), len(t))
    root = table("")
    rsec = alloc(root)
    desc = bytearray(S)
    desc[:20] = xdvdfs.MAGIC
    struct.pack_into("<II", desc, 0x14, rsec, len(root))
    desc[0x7EC:0x7EC + 20] = xdvdfs.MAGIC
    data[32] = bytes(desc)
    out = bytearray(nxt[0] * S)
    for sec, blob in data.items():
        out[sec * S:sec * S + len(blob)] = blob
    return bytes(out)


def write_image(path, files, base=0):
    """The partition at byte offset `base` of a (sparse) file."""
    with open(path, "wb") as f:
        f.seek(base)
        f.write(build_image(files))


def build_xbe(title=u"Test", entry=0x1D2807, sections=((".text", b"x"),)):
    """A minimal XBE: header, certificate, section headers and names, then data."""
    base, hdr_size = 0x10000, 0x1000
    h = bytearray(hdr_size)
    h[:4] = b"XBEH"
    cert_va, sec_va = base + 0x180, base + 0x400
    struct.pack_into("<8I", h, 0x104, base, hdr_size, 0x400000, 0x180, 0, cert_va, len(sections), sec_va)
    struct.pack_into("<I", h, 0x128, entry ^ 0xA8FC57AB)
    struct.pack_into("<I", h, 0x180 + 8, 0x4541005B)
    h[0x180 + 0x0C:0x180 + 0x0C + 2 * len(title)] = title.encode("utf-16le")
    struct.pack_into("<I", h, 0x180 + 0xA0, 1)
    blob, names_at, name_off = bytearray(), 0xC00, 0
    for i, (name, payload) in enumerate(sections):
        raddr = hdr_size + len(blob)
        blob += payload
        struct.pack_into("<6I", h, 0x400 + i * 0x38, 0, base + raddr, len(payload), raddr,
                         len(payload), base + names_at + name_off)
        h[names_at + name_off:names_at + name_off + len(name) + 1] = name.encode() + b"\0"
        name_off += len(name) + 1
    return bytes(h) + bytes(blob)


def dxt1_xpr(width, height, block):
    """An XPR0 resource with one DXT1 texture made of repeated `block`s."""
    fmt = 0x0C << 8 | (width.bit_length() - 1) << 20 | (height.bit_length() - 1) << 24 | 0x29
    data = block * ((width // 4) * (height // 4))
    return (b"XPR0" + struct.pack("<II", 0x800 + len(data), 0x800)
            + struct.pack("<IIIII", 0x40001, 0, 0, fmt, 0)).ljust(0x800, b"\0") + data


class XdvdfsTests(unittest.TestCase):
    FILES = {"default.xbe": b"XBE-bytes", "Data/a.bin": b"A" * 5000, "Data/Deep/b.bin": b"B",
             "empty.txt": b"", "Zed": b"Z" * 2048}

    def roundtrip(self, base):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "disc.iso")
            write_image(p, self.FILES, base)
            with xdvdfs.Image(p) as img:
                self.assertEqual(img.base, base)
                got = {e.path: img.read(e) for e in img.walk()}
                self.assertEqual(got, self.FILES)
                self.assertEqual(img.find("data\\DEEP/b.bin").size, 1)   # case-insensitive, either slash
                self.assertIsNone(img.find("Data/nope"))

    def test_plain_xiso(self):
        self.roundtrip(0)

    def test_redump_video_partition_offset(self):
        self.roundtrip(0x18300000)

    def test_not_an_image(self):
        with tempfile.NamedTemporaryFile() as f:
            f.write(b"\0" * 200000)
            f.flush()
            with self.assertRaises(xdvdfs.XdvdfsError):
                xdvdfs.Image(f.name)

    def test_unsafe_name_refused(self):
        with self.assertRaises(xdvdfs.XdvdfsError):
            xdvdfs._safe_name("..")
        with self.assertRaises(xdvdfs.XdvdfsError):
            xdvdfs._safe_name("a/b")


class DiscTests(unittest.TestCase):
    def setUp(self):
        self.xbe = build_xbe()
        self.builds = [{"id": "t", "name": "Test build", "xbe": {"sha256": xbe.sha256(self.xbe)}}]

    def test_image_install_is_restartable(self):
        with tempfile.TemporaryDirectory() as t:
            iso = os.path.join(t, "d.iso")
            write_image(iso, {"default.xbe": self.xbe, "Data/x.bin": b"x" * 9000}, 0x18300000)
            dest = os.path.join(t, "Game", "Burnout 3 Takedown")
            info = disc.install(iso, dest, self.builds)
            self.assertEqual(info["kind"], "image")
            self.assertEqual(os.path.getsize(os.path.join(dest, "Data", "x.bin")), 9000)
            # an interrupted file (a .part) is not mistaken for a finished one
            os.remove(os.path.join(dest, "Data", "x.bin"))
            open(os.path.join(dest, "Data", "x.bin.part"), "wb").write(b"half")
            disc.install(iso, dest, self.builds)
            self.assertEqual(os.path.getsize(os.path.join(dest, "Data", "x.bin")), 9000)
            self.assertEqual(disc.verify_installed(dest, self.builds)["id"], "t")

    def test_folder_install_one_level_down(self):
        with tempfile.TemporaryDirectory() as t:
            src = os.path.join(t, "wrapped", "Burnout 3 Takedown")
            os.makedirs(os.path.join(src, "Data"))
            open(os.path.join(src, "default.xbe"), "wb").write(self.xbe)
            open(os.path.join(src, "Data", "y.bin"), "wb").write(b"y")
            dest = os.path.join(t, "out")
            disc.install(os.path.join(t, "wrapped"), dest, self.builds)
            self.assertTrue(os.path.isfile(os.path.join(dest, "Data", "y.bin")))

    def test_other_xbe_is_refused_with_its_identity(self):
        other = build_xbe(title=u"Burnout 3 PAL")
        with self.assertRaises(disc.DiscError) as cm:
            disc.check_xbe(other, self.builds)
        self.assertEqual(cm.exception.code, "unsupported-xbe")
        self.assertIn("Burnout 3 PAL", str(cm.exception))

    def test_path_traversal_in_image_refused(self):
        with self.assertRaises(disc.DiscError):
            disc._safe_join("/tmp/x", "../evil")

    def test_supported_json_has_the_hash(self):
        b = disc.load_supported()[0]
        self.assertEqual(len(b["xbe"]["sha256"]), 64)


class XbeIconTests(unittest.TestCase):
    def test_xbe_header(self):
        x = xbe.Xbe(build_xbe(sections=[("$$XTIMAGE", b"abc"), (".text", b"zz")]))
        self.assertEqual((x.title_name, x.title_id, x.entry, x.region), ("Test", 0x4541005B, 0x1D2807, 1))
        self.assertEqual(x.section("$$XTIMAGE"), b"abc")
        self.assertIsNone(x.section("nope"))

    def test_dxt1_decode(self):
        red, blue = 0xF800, 0x001F
        # all indices 0 -> colour0 (red); then all indices 1 -> colour1 (blue)
        w, h, px = icons.decode_xpr(dxt1_xpr(8, 4, struct.pack("<HHI", red, blue, 0)))
        self.assertEqual((w, h, px[:4]), (8, 4, bytes((255, 0, 0, 255))))
        w, h, px = icons.decode_xpr(dxt1_xpr(4, 4, struct.pack("<HHI", red, blue, 0x55555555)))
        self.assertEqual(px[:4], bytes((0, 0, 255, 255)))
        # c0 <= c1: three-colour mode, index 3 is transparent
        w, h, px = icons.decode_xpr(dxt1_xpr(4, 4, struct.pack("<HHI", blue, red, 0xFFFFFFFF)))
        self.assertEqual(px[3], 0)

    def test_icon_files(self):
        blk = struct.pack("<HHI", 0xF800, 0x001F, 0)
        x = xbe.Xbe(build_xbe(sections=[("$$XTIMAGE", dxt1_xpr(128, 128, blk)),
                                        ("$$XSIMAGE", dxt1_xpr(64, 64, blk))]))
        with tempfile.TemporaryDirectory() as t:
            p = icons.make_icons(x, t)
            self.assertEqual(open(p["icns"], "rb").read(4), b"icns")
            ico = open(p["ico"], "rb").read()
            self.assertEqual(struct.unpack_from("<HHH", ico, 0), (0, 1, 6))
            for k in ("title_png", "png64", "png256", "save_png"):
                self.assertEqual(open(p[k], "rb").read(8), b"\x89PNG\r\n\x1a\n")

    def test_missing_title_image(self):
        with self.assertRaises(icons.IconError):
            icons.make_icons(xbe.Xbe(build_xbe()), tempfile.gettempdir() + "/never")


def synthetic_vdf(n=2):
    entries = [(str(i), [("appid", -1000 - i), ("AppName", "Game %d" % i), ("Exe", '"/g/%d.sh"' % i),
                         ("StartDir", "/g"), ("icon", ""), ("ShortcutPath", ""), ("LaunchOptions", "%command%"),
                         ("IsHidden", 0), ("tags", [("0", "x")])]) for i in range(n)]
    return steam.dump([("shortcuts", entries)])


class SteamTests(unittest.TestCase):
    def test_vdf_roundtrip_is_exact(self):
        d = synthetic_vdf(3)
        self.assertEqual(steam.dump(steam.parse(d)), d)
        with self.assertRaises(steam.VdfError):
            steam.parse(d[:-3])

    def test_add_keeps_existing_entries_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "shortcuts.vdf")
            open(p, "wb").write(synthetic_vdf(2))
            before = steam._entries(steam.parse(open(p, "rb").read()))
            aid, added = steam.add_shortcut(p, "Burnout 3: Takedown", "/app/burnout3.sh", "/app", "/art/icon.png")
            self.assertTrue(added)
            self.assertEqual(aid, steam.app_id('"/app/burnout3.sh"', "Burnout 3: Takedown"))
            self.assertGreaterEqual(aid, 0x80000000)
            after = steam._entries(steam.parse(open(p, "rb").read()))
            self.assertEqual(after[:2], before)                       # untouched
            new = dict(after[2][1])
            self.assertEqual((after[2][0], new["AppName"], new["Exe"], new["icon"]),
                             ("2", "Burnout 3: Takedown", '"/app/burnout3.sh"', "/art/icon.png"))
            self.assertEqual(new["appid"] & 0xFFFFFFFF, aid)
            self.assertEqual([k for k, _ in after[2][1]], [k for k, _ in before[1][1]])  # same field set
            self.assertEqual(len([n for n in os.listdir(t) if "backup" in n]), 1)
            self.assertEqual(steam.add_shortcut(p, "Burnout 3: Takedown", "/app/burnout3.sh", "/app", "x"),
                             (aid, False))
            self.assertEqual(len(steam._entries(steam.parse(open(p, "rb").read()))), 3)

    def login_file(self, t, users):
        body = "".join('\t"%d"\n\t{\n\t\t"AccountName"\t\t"a"\n%s\t}\n' % (
            steam.STEAMID64_BASE + uid, "".join('\t\t"%s"\t\t"%s"\n' % kv for kv in kvs)) for uid, kvs in users)
        p = os.path.join(t, "loginusers.vdf")
        open(p, "w").write('"users"\n{\n%s}\n' % body)
        return p

    def test_most_recent_user(self):
        with tempfile.TemporaryDirectory() as t:
            # MostRecent wins over a later Timestamp
            p = self.login_file(t, [(11, [("MostRecent", "0"), ("Timestamp", "900")]),
                                    (22, [("MostRecent", "1"), ("Timestamp", "100")])])
            self.assertEqual(steam.most_recent_user(p), "22")
            # no MostRecent (newer Steam): the highest Timestamp
            p = self.login_file(t, [(11, [("Timestamp", "900")]), (22, [("Timestamp", "100")])])
            self.assertEqual(steam.most_recent_user(p), "11")
            # cannot decide: a tie, no timestamps, missing file
            self.assertIsNone(steam.most_recent_user(self.login_file(t, [(11, [("Timestamp", "5")]),
                                                                          (22, [("Timestamp", "5")])])))
            self.assertIsNone(steam.most_recent_user(self.login_file(t, [(11, []), (22, [])])))
            self.assertIsNone(steam.most_recent_user(os.path.join(t, "nope")))

    def test_pick_user(self):
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(os.path.join(t, "config"))
            self.login_file(os.path.join(t, "config"), [(11, [("Timestamp", "1")]), (22, [("Timestamp", "2")])])
            users = [("11", "/s/userdata/11/config", t), ("22", "/s/userdata/22/config", t)]
            self.assertEqual(steam.pick_user(users)[0][0], "22")
            self.assertEqual(steam.pick_user(users[:1])[0][0], "11")
            self.assertIsNone(steam.pick_user([users[0], ("33", "/x", t)])[0])    # 22 has no folder: cannot choose
            self.assertIsNone(steam.pick_user([])[0])

    def test_creates_file_when_there_is_none(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "shortcuts.vdf")
            steam.add_shortcut(p, "G", "/x.sh", "/", "")
            self.assertEqual(len(steam._entries(steam.parse(open(p, "rb").read()))), 1)

    def test_art(self):
        x = xbe.Xbe(build_xbe(sections=[("$$XTIMAGE", dxt1_xpr(128, 128, struct.pack("<HHI", 0xF800, 0x1F, 0)))]))
        with tempfile.TemporaryDirectory() as t:
            art = steam.make_art(x, os.path.join(t, "grid"), 4095861016)
            self.assertEqual(sorted(os.path.basename(p) for p in art.values()),
                             ["4095861016.png", "4095861016_hero.png", "4095861016_icon.png", "4095861016p.png"])
            w, h = struct.unpack(">II", open(art["portrait"], "rb").read()[16:24])
            self.assertEqual((w, h), (600, 900))


class LinuxPackageTests(unittest.TestCase):
    class Out:
        def progress(self, *a):
            pass

        def log(self, *a):
            pass

    def make_ctx(self, t):
        ctx = type("Ctx", (), {})()
        ctx.out = self.Out()
        ctx.app_dir, ctx.root, ctx.work = (os.path.join(t, "app"), os.path.join(t, "root"), os.path.join(t, "work"))
        ctx.icons_dir = os.path.join(t, "icons")
        ctx.build_mode = "host"
        ctx.xdg_data_home = os.path.join(t, "xdg")
        os.makedirs(ctx.icons_dir)
        os.makedirs(os.path.join(ctx.work, "config"))
        open(os.path.join(ctx.work, "config", "enhance.toml.default"), "w").write("[render]\nscale = 1\n")
        open(os.path.join(ctx.icons_dir, "icon-256.png"), "wb").write(b"\x89PNG")
        open(os.path.join(t, "burnout3"), "wb").write(b"\x7fELF")
        return ctx

    def test_package_and_desktop(self):
        import subprocess
        with tempfile.TemporaryDirectory() as t:
            ctx = self.make_ctx(t)
            linux.package(ctx, os.path.join(t, "burnout3"), None)
            for n in ("burnout3", "burnout3.sh", "install.conf", "burnout3.png", "burnout3.desktop",
                      "enhance.toml.default"):
                self.assertTrue(os.path.exists(os.path.join(ctx.app_dir, n)), n)
            self.assertEqual(subprocess.call(["sh", "-n", os.path.join(ctx.app_dir, "burnout3.sh")]), 0)
            self.assertIn("root=" + ctx.root, open(os.path.join(ctx.app_dir, "install.conf")).read())
            dst = linux.desktop(ctx)
            text = open(dst).read()
            self.assertIn("Icon=" + linux.DESKTOP_ID, text)
            self.assertIn('Exec="%s"' % os.path.join(ctx.app_dir, "burnout3.sh"), text)
            self.assertTrue(os.path.exists(os.path.join(ctx.xdg_data_home, "icons", "hicolor", "256x256", "apps",
                                                       linux.DESKTOP_ID + ".png")))

    def test_launcher_refuses_without_game_data(self):
        import subprocess
        with tempfile.TemporaryDirectory() as t:
            ctx = self.make_ctx(t)
            linux.package(ctx, os.path.join(t, "burnout3"), None)
            r = subprocess.run([os.path.join(ctx.app_dir, "burnout3.sh")], stderr=subprocess.PIPE,
                               universal_newlines=True)
            self.assertEqual(r.returncode, 1)
            self.assertIn("game data not found", r.stderr)


class WindowsHelperTests(unittest.TestCase):
    def test_pins_are_complete(self):
        pins = win.pins()
        self.assertEqual(sorted(pins), ["cmake", "glslang", "llvm-mingw", "ninja", "sdl2", "vulkan-headers"])
        for e in pins.values():
            self.assertRegex(e["sha256"], r"^[0-9a-f]{64}$")
            self.assertTrue(e["url"].startswith("https://"))

    def test_extract_strips_and_refuses_escapes(self):
        import io
        import tarfile
        import zipfile
        with tempfile.TemporaryDirectory() as t:
            z = os.path.join(t, "a.zip")
            with zipfile.ZipFile(z, "w") as f:
                f.writestr("top/bin/tool.exe", b"x")
                f.writestr("top/", b"")
            win.extract(z, os.path.join(t, "out"), strip=1)
            self.assertTrue(os.path.isfile(os.path.join(t, "out", "bin", "tool.exe")))
            bad = os.path.join(t, "bad.zip")
            with zipfile.ZipFile(bad, "w") as f:
                f.writestr("top/../../evil.txt", b"x")
            with self.assertRaises(common.StepError):
                win.extract(bad, os.path.join(t, "out2"), strip=1)
            tg = os.path.join(t, "a.tar.gz")
            with tarfile.open(tg, "w:gz") as f:
                info = tarfile.TarInfo("top/ok.txt")
                info.size = 2
                f.addfile(info, io.BytesIO(b"ok"))
                link = tarfile.TarInfo("top/link")
                link.type = tarfile.SYMTYPE
                link.linkname = "/etc/passwd"
                f.addfile(link)
            win.extract(tg, os.path.join(t, "out3"), strip=1)
            self.assertEqual(os.listdir(os.path.join(t, "out3")), ["ok.txt"])        # the symlink is dropped

    def test_download_checks_the_hash(self):
        import hashlib
        with tempfile.TemporaryDirectory() as t:
            src = os.path.join(t, "src.bin")
            open(src, "wb").write(b"payload")
            url = "file://" + src
            dest = os.path.join(t, "dl", "x.bin")
            win.download(url, dest, hashlib.sha256(b"payload").hexdigest())
            self.assertEqual(open(dest, "rb").read(), b"payload")
            with self.assertRaises(common.StepError):
                win.download(url, os.path.join(t, "dl", "y.bin"), "0" * 64)
            self.assertFalse(os.path.exists(os.path.join(t, "dl", "y.bin")))         # nothing half-written is kept

    def test_tool_env_puts_portable_tools_first_for_the_child_only(self):
        ctx = type("C", (), {})()
        ctx.tools_dir, ctx.env = os.path.join("T", "tools"), {"PATH": "orig"}
        env = win.tool_env(ctx)
        self.assertTrue(env["PATH"].startswith(os.path.join("T", "tools", "llvm-mingw", "bin")))
        self.assertEqual(ctx.env["PATH"], "orig")


class ToolchainReportTests(unittest.TestCase):
    def test_no_pip_reports_everything_in_one_pass(self):
        import subprocess
        with tempfile.TemporaryDirectory() as t:
            r = subprocess.run([sys.executable, "-I", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                                                    "install.py"),
                                "--only", "toolchain", "--no-pip", "--python", sys.executable, "--work", t,
                                "--root", os.path.join(t, "root"), "--toolkit", os.path.join(t, "none")],
                               stdout=subprocess.PIPE, universal_newlines=True)
            missing = [l.split()[2] for l in r.stdout.splitlines() if l.startswith("MISSING")]
            self.assertIn("toolkit", missing)                     # reported even when capstone is the first problem
            self.assertIn("source", missing)
            self.assertEqual(len(missing), len(set(missing)))      # one line each
            self.assertEqual(r.returncode, 3)


class LocationTests(unittest.TestCase):
    DRIVER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "install.py")

    def test_nested_nonexistent_path_is_measured_through_its_parent(self):
        with tempfile.TemporaryDirectory() as t:
            deep = os.path.join(t, "no", "such", "nested", "folder")
            self.assertEqual(common.nearest_existing(deep), os.path.abspath(t))
            ok, detail = common.check_location(deep, 0.001)
            self.assertTrue(ok, detail)
            self.assertFalse(common.check_location(deep, 10 ** 9)[0])           # too little space is reported, not raised
            self.assertFalse(os.path.exists(os.path.join(t, "no")))             # and nothing was created

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0), "needs a POSIX non-root user")
    def test_unwritable_parent_is_a_failure_not_a_traceback(self):
        import subprocess
        with tempfile.TemporaryDirectory() as t:
            ro = os.path.join(t, "ro")
            os.mkdir(ro)
            os.chmod(ro, 0o500)
            try:
                ok, detail = common.check_location(os.path.join(ro, "inner", "deeper"))
                self.assertFalse(ok)
                self.assertIn("cannot write", detail)
            finally:
                os.chmod(ro, 0o700)

    def run_driver(self, *args):
        import subprocess
        r = subprocess.run([sys.executable, "-I", self.DRIVER] + list(args), stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, universal_newlines=True)
        return r

    def test_toolchain_with_nothing_created_yet_has_no_traceback(self):
        with tempfile.TemporaryDirectory() as t:
            r = self.run_driver("--only", "toolchain", "--no-pip", "--no-fetch", "--python", sys.executable,
                                "--work", t, "--toolkit", os.path.join(t, "none"),
                                "--root", os.path.join(t, "a", "b", "root"), "--app-dir", os.path.join(t, "c", "d", "app"),
                                "--tools-dir", os.path.join(t, "e", "f", "tools"))
            self.assertNotIn("Traceback", r.stdout + r.stderr)
            self.assertTrue(r.stdout.rstrip().endswith("RESULT FAIL toolchain toolchain-incomplete"), r.stdout[-300:])

    @unittest.skipIf(os.name == "nt" or (hasattr(os, "geteuid") and os.geteuid() == 0), "needs a POSIX non-root user")
    def test_unwritable_root_is_reported_by_the_toolchain_step(self):
        with tempfile.TemporaryDirectory() as t:
            ro = os.path.join(t, "ro")
            os.mkdir(ro)
            os.chmod(ro, 0o500)
            try:
                r = self.run_driver("--only", "toolchain", "--root", os.path.join(ro, "x", "root"), "--work", t)
            finally:
                os.chmod(ro, 0o700)
            self.assertNotIn("Traceback", r.stdout + r.stderr)
            self.assertIn("MISSING toolchain disk-space", r.stdout)
            self.assertIn("cannot write to", r.stdout)
            self.assertTrue(r.stdout.rstrip().endswith("RESULT FAIL toolchain toolchain-incomplete"))
            self.assertEqual(r.returncode, 3)

    def test_unexpected_exception_becomes_a_coded_failure_with_a_log(self):
        with tempfile.TemporaryDirectory() as t:
            r = self.run_driver("--only", "icon", "--root", os.path.join(t, "root"))
            self.assertNotIn("Traceback", r.stdout + r.stderr)
            self.assertIn("STEP icon FAIL io-error", r.stdout)
            self.assertIn("RESULT FAIL icon io-error", r.stdout)
            self.assertEqual(r.returncode, 1)
            self.assertIn("Traceback", open(os.path.join(t, "root", "install", "logs", "icon.log")).read())


class RegenCommandTests(unittest.TestCase):
    def test_stage_commands_follow_regen_sh(self):
        calls = []

        def fake_run(ctx, step, cmd, cwd=None, env=None, tee=None, **kw):
            calls.append((cmd, cwd, env, tee))
            if tee:
                open(tee, "w").close()
            return 0
        real = regen.run
        regen.run = fake_run
        try:
            with tempfile.TemporaryDirectory() as t:
                os.makedirs(os.path.join(t, "work", "config"))
                open(os.path.join(t, "work", "config", "seed_functions.json"), "w").write("{}")
                open(os.path.join(t, "work", "config", "spin_waits.json"), "w").write("{}")
                ctx = type("C", (), {})()
                ctx.env, ctx.logs = {}, t
                stages = []
                regen.regen(ctx, "py", "game/default.xbe", os.path.join(t, "xr"), "GEN", "TK", os.path.join(t, "work"),
                            on_stage=stages.append, site="SITE")
        finally:
            regen.run = real
        self.assertEqual(stages, list(regen.STAGES))
        mods = [c[0][3] for c in calls]
        self.assertEqual(mods, ["tools.xbe_parser", "tools.disasm", "tools.func_id", "tools.abi_analysis", "tools.recomp"])
        self.assertTrue(all(c[1] == "TK" and c[2]["BO3_SITE"] == "SITE" for c in calls))
        recomp = calls[-1][0]
        for flag in ("--all", "--split", "--exclude-manual", "--spin-waits", "--gen-dir"):
            self.assertIn(flag, recomp)
        self.assertIn("--seed-functions", calls[1][0])


@unittest.skipUnless(sys.platform == "darwin", "macOS tools")
class BundleTests(unittest.TestCase):
    def test_parse_otool(self):
        text = ("/x/burnout3:\n\t/opt/homebrew/lib/libfoo.1.dylib (compatibility version 1.0.0, current version 1.0.0)\n"
                "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n")
        self.assertEqual(bundle.parse_otool_l(text),
                         ["/opt/homebrew/lib/libfoo.1.dylib", "/usr/lib/libSystem.B.dylib"])

    def test_bundle_recursive_and_audit(self):
        import subprocess
        with tempfile.TemporaryDirectory() as t:
            t = os.path.realpath(t)
            fake = os.path.join(t, "brew", "lib")
            os.makedirs(fake)
            def cc(*a):
                subprocess.check_call(["cc"] + list(a))
            open(os.path.join(t, "b.c"), "w").write("int b(void){return 40;}\n")
            open(os.path.join(t, "a.c"), "w").write("int b(void); int a(void){return b()+1;}\n")
            open(os.path.join(t, "m.c"), "w").write("int a(void); int main(void){return a()==41?0:1;}\n")
            cc("-dynamiclib", os.path.join(t, "b.c"), "-install_name", fake + "/libb.dylib", "-o", fake + "/libb.dylib")
            cc("-dynamiclib", os.path.join(t, "a.c"), fake + "/libb.dylib", "-install_name", fake + "/liba.dylib",
               "-o", fake + "/liba.dylib")
            exe = os.path.join(t, "App.app", "Contents", "MacOS", "m")
            os.makedirs(os.path.dirname(exe))
            cc(os.path.join(t, "m.c"), fake + "/liba.dylib", "-o", exe)
            self.assertTrue(bundle.audit([exe]))                       # linked to the fake brew
            fw = os.path.join(t, "App.app", "Contents", "Frameworks")
            names = bundle.bundle(exe, fw)
            self.assertEqual(names, ["liba.dylib", "libb.dylib"])      # b came in through a
            import plistlib
            with open(os.path.join(t, "App.app", "Contents", "Info.plist"), "wb") as f:
                plistlib.dump({"CFBundleExecutable": "m", "CFBundleIdentifier": "test.m",
                               "CFBundlePackageType": "APPL"}, f)
            bundle.sign(os.path.join(t, "App.app"), fw)
            self.assertEqual(bundle.audit([exe] + [os.path.join(fw, n) for n in names]), [])
            shutil_rm = __import__("shutil").rmtree
            shutil_rm(os.path.join(t, "brew"))                         # the original libraries are gone
            self.assertEqual(subprocess.call([exe]), 0)                # and it still runs


if __name__ == "__main__":
    unittest.main()


class RegionTest(unittest.TestCase):
    """The PAL disc builds the PAL program (docs/pal.md)."""

    def test_every_build_names_its_region(self):
        regions = {b["id"]: b["bo3_region"] for b in disc.load_supported()}
        self.assertEqual(regions, {"ntsc-u-retail": "NTSC-U", "pal-retail": "PAL"})

    def test_regen_inputs(self):
        import regen
        seeds, spin, manual = regen.inputs("/w", "PAL")
        self.assertEqual((seeds, spin, manual), (os.path.join("/w", "config", "pal", "seed_functions.json"),
                                                 os.path.join("/w", "config", "pal", "spin_waits.json"),
                                                 os.path.join("/w", "src", "game", "recomp", "recomp_manual_pal.c")))
        self.assertEqual(regen.inputs("/w")[2], os.path.join("/w", "src", "game", "recomp", "recomp_manual.c"))
        for p in regen.inputs(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."), "PAL"):
            self.assertTrue(os.path.isfile(p), p)

    def test_region_of_the_installed_xbe(self):
        import install
        import xbe as xbe_mod
        pal = next(b for b in disc.load_supported() if b["id"] == "pal-retail")
        with tempfile.TemporaryDirectory() as t:
            open(os.path.join(t, "default.xbe"), "wb").close()
            ctx = type("C", (), {"game_dir": t})()
            real = xbe_mod.sha256_file
            try:
                xbe_mod.sha256_file = lambda p: pal["xbe"]["sha256"]
                self.assertEqual(install.region_of(ctx), "PAL")
                xbe_mod.sha256_file = lambda p: "0" * 64
                self.assertEqual(install.region_of(ctx), "NTSC-U")
            finally:
                xbe_mod.sha256_file = real
