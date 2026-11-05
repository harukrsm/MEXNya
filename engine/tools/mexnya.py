#!/usr/bin/env python3
"""MEXNya engine build tool: create, build, test, package and edit games made with MEXNya.

The project describes itself in mexnya.json (targets, packages, banner); this tool knows
nothing about any particular game. It finds the compilers (the host's clang/gcc through
CMake, a cross compiler inside a Docker image), configures and builds each target with a
one-line progress bar, colours warnings and errors, and opens with the engine banner in
colour.

    python3 engine/tools/mexnya.py new <folder> [--name NAME]   # a new game from the template
    python3 engine/tools/mexnya.py build [debug|release|windows|all ...]
    python3 engine/tools/mexnya.py package [macos|windows|all]
    python3 engine/tools/mexnya.py test
    python3 engine/tools/mexnya.py assets
    python3 engine/tools/mexnya.py doctor      # which compilers would be used
    python3 engine/tools/mexnya.py banner
    python3 engine/tools/mexnya.py run         # build and open the MEXNya Editor (GUI)
    python3 engine/tools/mexnya.py agent <op> ['<json args>'] [--as NAME]
                                               # one editor operation, for AI agents

`agent` talks to the open editor through mexnya/agent/inbox (the person sees it
happen); with no editor open it runs the operation headless on the project files.
`agent help` lists every operation; mexnya/AGENTS.md explains the project to an agent.

Colour follows the terminal: none when piped or NO_COLOR is set, 24-bit when COLORTERM
says so, the 256-colour cube otherwise. FORCE_COLOR=1 forces it on.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.dirname(HERE)
ENGINE_ROOT = os.path.dirname(ENGINE)      # the git-MEXNya repository
DEFAULT_BANNER = os.path.join(ENGINE, "resources", "banner.txt")


# ---------------------------------------------------------------- colour
def colour_mode():
    if os.environ.get("NO_COLOR"):
        return 0
    if not os.environ.get("FORCE_COLOR") and not sys.stdout.isatty():
        return 0
    ct = os.environ.get("COLORTERM", "")
    return 24 if ("truecolor" in ct or "24bit" in ct) else 8


MODE = colour_mode()


def fg(rgb):
    if not MODE:
        return ""
    r, g, b = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    if MODE == 24:
        return "\x1b[38;2;%d;%d;%dm" % (r, g, b)
    q = lambda v: 0 if v < 48 else (1 if v < 115 else (v - 35) // 40)
    return "\x1b[38;5;%dm" % (16 + 36 * q(r) + 6 * q(g) + q(b))


RESET = "\x1b[0m" if MODE else ""
DIM = 0x6E7275
TEXT = 0xADB6BD
ACCENT = 0xD9C48F
WARN = 0xC9A63C
ERR = 0xC0504A
OK = 0x6E9A72

# The rainbow under the title: blue, green, amber, red, violet.
RAINBOW = [0x2E6FA8, 0x5C9B57, 0xD08A3C, 0xC0392B, 0x7A4E8C]


def lerp_rgb(a, b, t):
    ch = lambda s: int(((a >> s) & 255) + (((b >> s) & 255) - ((a >> s) & 255)) * t)
    return (ch(16) << 16) | (ch(8) << 8) | ch(0)


def gradient(t):
    t = min(max(t, 0.0), 1.0) * (len(RAINBOW) - 1)
    i = min(int(t), len(RAINBOW) - 2)
    return lerp_rgb(RAINBOW[i], RAINBOW[i + 1], t - i)


def shade(rgb, k):
    return lerp_rgb(0, rgb, k)


def say(text, rgb=TEXT, end="\n"):
    sys.stdout.write(fg(rgb) + text + RESET + end)
    sys.stdout.flush()


def print_banner(path):
    try:
        lines = open(path, encoding="utf-8").read().rstrip("\n").split("\n")
    except OSError:
        return
    width = max((len(l) for l in lines), default=1) or 1
    if not MODE:
        print("\n".join(lines))
        return
    # Colour runs left to right through the rainbow; light shade blocks are dimmer, and each
    # row a little darker than the one above, like the banner sinking into fog.
    LIGHT = {"░": 0.45, "▒": 0.62, "▓": 0.8}
    out = []
    for row, line in enumerate(lines):
        s = []
        for col, ch in enumerate(line):
            if ch == " ":
                s.append(ch)
                continue
            k = LIGHT.get(ch, 1.0) * (1.0 - 0.05 * row)
            s.append(fg(shade(gradient(col / width), k)) + ch)
        out.append("".join(s) + RESET)
    print("\n".join(out))


# ---------------------------------------------------------------- project
class Project:
    def __init__(self, root):
        self.root = root
        with open(os.path.join(root, "mexnya.json"), encoding="utf-8") as f:
            self.cfg = json.load(f)
        self.name = self.cfg.get("name", "game")
        self.version = self.cfg.get("version", "")

    def path(self, rel):
        return os.path.join(self.root, rel)


def find_root():
    d = os.getcwd()
    while True:
        if os.path.exists(os.path.join(d, "mexnya.json")):
            return d
        up = os.path.dirname(d)
        if up == d:
            sys.exit("mexnya: no mexnya.json here or above")
        d = up


# ---------------------------------------------------------------- compilers
def first_line(cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return (r.stdout or r.stderr).strip().split("\n")[0]
    except (OSError, subprocess.TimeoutExpired):
        return None


def host_compiler(build_dir):
    cache = os.path.join(build_dir, "CMakeCache.txt")
    cc = None
    if os.path.exists(cache):
        m = re.search(r"^CMAKE_C_COMPILER:\w+=(.*)$", open(cache).read(), re.M)
        cc = m.group(1).strip() if m else None
    cc = cc or os.environ.get("CC") or shutil.which("cc") or shutil.which("clang") or shutil.which("gcc")
    return cc, (first_line([cc, "--version"]) if cc else None)


def docker_ok(image):
    if not shutil.which("docker"):
        return False
    r = subprocess.run(["docker", "image", "inspect", image], capture_output=True)
    return r.returncode == 0


def docker_cmd(p, image, args):
    return ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", p.root + ":/src", "-w", "/src", image] + args


# ---------------------------------------------------------------- running
PROGRESS = re.compile(r"^\[\s*(\d+)%\]")
WARNING = re.compile(r"(warning:|error:|Error|FAILED)")


def bar(pct, width=24):
    n = int(width * pct / 100)
    return "[" + "#" * n + "." * (width - n) + "]"


def run_step(label, cmd, cwd, show_progress=True):
    """Runs a command, showing a progress bar from CMake's [ nn%] lines and keeping warnings
    and errors. Returns (ok, seconds)."""
    t0 = time.time()
    tty = bool(MODE) or sys.stdout.isatty()
    pct = 0
    tail = []
    seen = set()
    proc = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            errors="replace")
    for line in proc.stdout:
        line = line.rstrip("\n")
        tail = (tail + [line])[-60:]
        m = PROGRESS.match(line)
        if m:
            pct = int(m.group(1))
        if WARNING.search(line) and line not in seen:
            seen.add(line)
            if tty:
                sys.stdout.write("\r\x1b[K")
            say("    " + line, ERR if ("error" in line.lower() or "FAILED" in line) else WARN)
        if show_progress and tty:
            sys.stdout.write("\r\x1b[K" + fg(DIM) + "  %-28s %s %3d%%" % (label, bar(pct), pct) + RESET)
            sys.stdout.flush()
    proc.wait()
    dt = time.time() - t0
    if tty:
        sys.stdout.write("\r\x1b[K")
    if proc.returncode != 0:
        say("  %-28s failed (%.1fs)" % (label, dt), ERR)
        for l in tail[-30:]:
            say("    " + l, DIM)
        return False, dt
    say("  %-28s " % label, TEXT, end="")
    say("ok", OK, end="")
    say("  %.1fs" % dt, DIM)
    return True, dt


def build_target(p, name):
    t = p.cfg["targets"].get(name)
    if not t:
        sys.exit("mexnya: unknown target '%s' (have: %s)" % (name, ", ".join(p.cfg["targets"])))
    defines = ["-D%s=%s" % kv for kv in t.get("defines", {}).items()]
    if t.get("docker"):
        image = t["docker"]
        if not docker_ok(image):
            say("  %-28s skipped: Docker image %s is not available" % (name, image), WARN)
            return False
        tc = ["-DCMAKE_TOOLCHAIN_FILE=/src/" + t["toolchain"]] if t.get("toolchain") else []
        ver = first_line(docker_cmd(p, image, [t["compiler"], "--version"])) if t.get("compiler") else None
        say("  %s: %s (in %s)" % (name, ver or "cross compiler", image), DIM)
        ok, _ = run_step("configure " + name,
                         docker_cmd(p, image, ["cmake", "-S", ".", "-B", t["dir"], "-DCMAKE_BUILD_TYPE=" + t["type"]]
                                    + tc + defines), p.root, False)
        if not ok:
            return False
        ok, _ = run_step("compile " + name,
                         docker_cmd(p, image, ["cmake", "--build", t["dir"], "-j" + os.environ.get("JOBS", "8")]),
                         p.root)
        return ok
    bdir = p.path(t["dir"])
    ok, _ = run_step("configure " + name, ["cmake", "-S", p.root, "-B", bdir, "-DCMAKE_BUILD_TYPE=" + t["type"]]
                     + defines, p.root, False)
    if not ok:
        return False
    cc, ver = host_compiler(bdir)
    say("  %s: %s" % (name, ver or cc or "unknown compiler"), DIM)
    ok, _ = run_step("compile " + name, ["cmake", "--build", bdir, "-j" + os.environ.get("JOBS", "8")], p.root)
    return ok


def header(p, what):
    banner = p.path(p.cfg["banner"]) if p.cfg.get("banner") else DEFAULT_BANNER
    print_banner(banner)
    say("  MEXNya engine", ACCENT, end="")
    say("  ·  %s %s  ·  %s" % (p.name, p.version, what), DIM)
    print()


def cmd_build(p, names):
    names = names or ["debug"]
    if names == ["all"]:
        names = list(p.cfg["targets"])
    header(p, "build " + " ".join(names))
    ok = all([build_target(p, n) for n in names])
    print()
    say("  done" if ok else "  build failed", OK if ok else ERR)
    return 0 if ok else 1


def cmd_package(p, names):
    pk = p.cfg.get("packages", {})
    names = names or list(pk)
    if names == ["all"]:
        names = list(pk)
    header(p, "package " + " ".join(names))
    for n in names:
        if n not in pk:
            sys.exit("mexnya: unknown package '%s' (have: %s)" % (n, ", ".join(pk)))
        if not build_target(p, pk[n]["target"]):
            return 1
        ok, _ = run_step("package " + n, ["bash", p.path(pk[n]["script"])], p.root, False)
        if not ok:
            return 1
    print()
    say("  packages are in build/dist/", OK)
    return 0


def cmd_test(p):
    t = p.cfg.get("test")
    header(p, "test")
    if not t:
        say("  no \"test\" section in mexnya.json", WARN)
        return 1
    if not build_target(p, t["target"]):
        return 1
    ok, _ = run_step("tests", ["bash", p.path(t["script"])] + t.get("args", []), p.root, False)
    return 0 if ok else 1


def cmd_assets(p):
    header(p, "assets")
    if not p.cfg.get("assets"):
        say("  no \"assets\" command in mexnya.json", WARN)
        return 1
    ok, _ = run_step("generate assets", p.cfg["assets"], p.root, False)
    return 0 if ok else 1


def cmd_doctor(p):
    header(p, "doctor")
    say("  cmake      " + (first_line(["cmake", "--version"]) or "missing"), TEXT)
    for name, t in p.cfg["targets"].items():
        if t.get("docker"):
            have = docker_ok(t["docker"])
            ver = first_line(docker_cmd(p, t["docker"], [t["compiler"], "--version"])) if have and t.get("compiler") else None
            say("  %-10s %s" % (name, (ver + "  (Docker " + t["docker"] + ")") if ver else
                                     "Docker image %s missing" % t["docker"]), TEXT if ver else WARN)
        else:
            cc, ver = host_compiler(p.path(t["dir"]))
            say("  %-10s %s" % (name, ver or "no C compiler found"), TEXT if ver else WARN)
    return 0


# ---------------------------------------------------------------- editor
def editor_exe():
    """The editor is part of the engine: it is built once, in the engine repository."""
    return os.path.join(ENGINE_ROOT, "build", "editor", "MexnyaEditor" + (".exe" if os.name == "nt" else ""))


def build_editor(p=None, quiet=False):
    bdir = os.path.join(ENGINE_ROOT, "build", "editor")
    if not os.path.exists(os.path.join(bdir, "CMakeCache.txt")):
        ok, _ = run_step("configure editor", ["cmake", "-S", ENGINE_ROOT, "-B", bdir, "-DCMAKE_BUILD_TYPE=Release",
                                              "-DMEXNYA_BUILD_SAMPLES=OFF"], ENGINE_ROOT, False)
        if not ok:
            return None
    ok, _ = run_step("compile editor", ["cmake", "--build", bdir, "--target", "MexnyaEditor",
                                        "-j" + os.environ.get("JOBS", "8")], ENGINE_ROOT, not quiet)
    return editor_exe() if ok else None


def cmd_run(p, rest):
    header(p, "editor")
    exe = editor_exe() if "--no-build" in rest else build_editor(p)
    if not exe or not os.path.exists(exe):
        say("  the editor did not build", ERR)
        return 1
    say("  opening MEXNya Editor  ·  project " + p.root, OK)
    args = [exe, "--project", p.root] + [a for a in rest if a != "--no-build"]
    return subprocess.call(args)


def editor_alive(p):
    try:
        with open(p.path("mexnya/agent/editor.json")) as f:
            beat = json.load(f)
        return time.time() - float(beat.get("time", 0)) < 4.0
    except (OSError, ValueError):
        return False


def cmd_agent(p, rest):
    who = os.environ.get("MEXNYA_AGENT", "agent")
    if "--as" in rest:
        i = rest.index("--as")
        who = rest[i + 1] if i + 1 < len(rest) else who
        rest = rest[:i] + rest[i + 2:]
    if not rest:
        rest = ["help"]
    op = rest[0]
    raw = rest[1] if len(rest) > 1 else "{}"
    if raw == "-":
        raw = sys.stdin.read()
    try:
        args = json.loads(raw)
    except ValueError as e:
        print(json.dumps({"ok": False, "error": "args are not JSON: %s" % e}))
        return 2
    req = {"agent": who, "op": op, "args": args}
    if op == "batch":
        req = {"agent": who, "batch": args}
    if editor_alive(p):
        rid = "%d-%d" % (int(time.time() * 1000), os.getpid())
        inbox = p.path("mexnya/agent/inbox")
        os.makedirs(inbox, exist_ok=True)
        tmp = os.path.join(inbox, rid + ".tmp")
        with open(tmp, "w") as f:
            json.dump(req, f, ensure_ascii=False)
        os.replace(tmp, os.path.join(inbox, rid + ".json"))
        out = p.path("mexnya/agent/outbox/%s.json" % rid)
        for _ in range(600):
            if os.path.exists(out):
                time.sleep(0.02)
                with open(out) as f:
                    text = f.read()
                os.remove(out)
                print(text.strip())
                return 0 if '"ok": true' in text or '"ok":true' in text else 1
            time.sleep(0.05)
        print(json.dumps({"ok": False, "error": "the editor did not answer in 30 s"}))
        return 1
    exe = editor_exe()
    if not os.path.exists(exe):
        exe = build_editor(p, quiet=True)
        if not exe:
            return 1
    return subprocess.call([exe, "--project", p.root, "--as", who, "--exec", json.dumps(req, ensure_ascii=False)])


# ---------------------------------------------------------------- new game
def cmd_new(rest):
    if not rest or rest[0].startswith("-"):
        sys.exit("mexnya: new <folder> [--name NAME]")
    dest = os.path.abspath(rest[0])
    name = os.path.basename(dest.rstrip("/\\"))
    if "--name" in rest:
        i = rest.index("--name")
        if i + 1 < len(rest):
            name = rest[i + 1]
    name = re.sub(r"[^A-Za-z0-9_-]", "_", name) or "game"
    if os.path.exists(dest) and os.listdir(dest):
        sys.exit("mexnya: %s exists and is not empty" % dest)
    src = os.path.join(ENGINE_ROOT, "templates", "game")
    # Relative when the engine is close by (both move together), absolute otherwise.
    engine_rel = os.path.relpath(ENGINE_ROOT, dest).replace("\\", "/")
    if engine_rel.count("..") > 2:
        engine_rel = ENGINE_ROOT.replace("\\", "/")
    for base, dirs, files in os.walk(src):
        rel = os.path.relpath(base, src)
        os.makedirs(os.path.join(dest, rel), exist_ok=True)
        for fn in files:
            with open(os.path.join(base, fn), encoding="utf-8") as f:
                text = f.read()
            text = text.replace("__GAME_NAME__", name)
            if fn == "CMakeLists.txt" and os.path.isabs(engine_rel):
                text = text.replace("${CMAKE_CURRENT_SOURCE_DIR}/__ENGINE_DIR__", engine_rel)
            text = text.replace("__ENGINE_DIR__", engine_rel)
            with open(os.path.join(dest, rel, fn), "w", encoding="utf-8") as f:
                f.write(text)
    print_banner(DEFAULT_BANNER)
    say("  new game ", TEXT, end="")
    say(name, ACCENT, end="")
    say("  ·  " + dest, DIM)
    tool = os.path.join(ENGINE_ROOT, "bin", "mexnya")
    say("  next (with %s on your PATH, or call it by its full path):" % os.path.dirname(tool), TEXT)
    say('    cd "%s"' % rest[0], DIM)
    say("    mexnya build        # compile (build/debug)", DIM)
    say("    mexnya run          # open the editor on this game", DIM)
    return 0


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help", "help"):
        print(__doc__.strip())
        return 0
    cmd, rest = args[0], args[1:]
    if cmd == "new":
        return cmd_new(rest)
    p = Project(find_root())
    if cmd == "build":
        return cmd_build(p, rest)
    if cmd == "package":
        return cmd_package(p, rest)
    if cmd == "test":
        return cmd_test(p)
    if cmd == "assets":
        return cmd_assets(p)
    if cmd == "doctor":
        return cmd_doctor(p)
    if cmd in ("run", "editor"):
        return cmd_run(p, rest)
    if cmd == "agent":
        return cmd_agent(p, rest)
    if cmd == "banner":
        print_banner(p.path(p.cfg.get("banner", "")))
        return 0
    sys.exit("mexnya: unknown command '%s' (new, build, package, test, assets, doctor, banner, run, agent)" % cmd)


if __name__ == "__main__":
    sys.exit(main() or 0)
