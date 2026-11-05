#!/usr/bin/env python3
"""Dependency boundaries of the MEXNya engine.

- Only the platform and renderer backends touch SDL or OpenGL
  (engine/platform/desktop/, engine/render/gl/); only the Lua glue touches Lua
  (engine/console/lua_console.c, editor/script.c).
- No engine header exposes SDL, OpenGL or Lua types, so games stay backend-free.
- The editor, samples and templates use the engine API, never SDL/GL directly.
Exit status 1 on a violation.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDL_GL = re.compile(r'#include\s*[<"]((?:SDL3?/|SDL\.h|GL/|OpenGL/)[^>"]*)[>"]')
LUA = re.compile(r'#include\s*[<"]((?:lua|lauxlib|lualib)\.h)[>"]')
TYPES = re.compile(r'\b(SDL_[A-Za-z0-9_]+|GLuint|GLenum|GLint|lua_State)\b')
SDL_GL_OK = ("engine/platform/desktop/", "engine/render/gl/")
LUA_OK = ("engine/console/lua_console.c", "editor/script.c")


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def sources():
    for top in ("engine", "editor", "samples", "templates"):
        for dp, _, names in os.walk(os.path.join(ROOT, top)):
            for n in names:
                if n.endswith((".c", ".h")):
                    p = os.path.join(dp, n)
                    yield os.path.relpath(p, ROOT).replace(os.sep, "/"), p


def main():
    bad = []
    for rel, path in sources():
        text = strip_comments(open(path, encoding="utf-8", errors="replace").read())
        for m in SDL_GL.finditer(text):
            if not rel.startswith(SDL_GL_OK):
                bad.append("%s: includes %s (only platform/render backends may)" % (rel, m.group(1)))
        for m in LUA.finditer(text):
            if rel not in LUA_OK:
                bad.append("%s: includes %s (only the Lua glue may)" % (rel, m.group(1)))
        if rel.startswith("engine/") and rel.endswith(".h") and not rel.startswith(SDL_GL_OK):
            for m in TYPES.finditer(text):
                bad.append("%s: public header exposes %s" % (rel, m.group(1)))
    for b in bad:
        print("boundary: " + b)
    if bad:
        return 1
    print("boundary check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
