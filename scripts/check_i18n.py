#!/usr/bin/env python3
"""Guards the rule "code is English; only the UI is translated".

1. No CJK, kana or Hangul string literal in editor/ or engine/ C code (the language
   names in engine/core/i18n.c are the one exception: they name themselves).
2. Every translation key the editor uses exists in English.
3. Every language has exactly the English key set, and every text keeps the English
   printf conversions, so no language switch can break a format call or a lookup.
Exit status 1 on any failure.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
LANGS = ["en", "zh-Hans", "zh-Hant", "ja", "ko"]
WIDE = re.compile(r'"[^"\n]*[぀-ヿ㐀-鿿가-힣＀-￯][^"\n]*"')
KEY = re.compile(r'"((?:tab|top|status|tool|panel|menu|type|field|port|note|button|console|view|graph|build|git|agent|plugins|settings)\.[a-z0-9_]+)"')
OP = re.compile(r'reg\("([a-z_.]+)"')


def conversions(s):
    out = []
    i = 0
    while i < len(s):
        if s[i] == "%":
            i += 1
            if i < len(s) and s[i] == "%":
                i += 1
                continue
            while i < len(s) and s[i] in "-+ #0123456789.*lhzjt":
                i += 1
            if i >= len(s):
                return None
            c = s[i]
            out.append({"i": "d", "u": "d", "x": "d", "X": "d", "o": "d", "e": "f", "g": "f", "E": "f", "G": "f"}.get(c, c))
        i += 1
    return "".join(out)


def main():
    errors = []
    sources = glob.glob(os.path.join(ROOT, "editor", "*.c")) + glob.glob(os.path.join(ROOT, "engine", "**", "*.c"), recursive=True)
    for path in sources:
        rel = os.path.relpath(path, ROOT)
        if rel.endswith("core/i18n.c"):
            continue
        for n, line in enumerate(open(path, encoding="utf-8"), 1):
            if WIDE.search(line) and not line.strip().startswith("//"):
                errors.append("%s:%d: user text in code, use tr(\"key\"): %s" % (rel, n, line.strip()[:80]))
    tables = {}
    for lang in LANGS:
        p = os.path.join(ROOT, "engine", "resources", "lang", "editor.%s.json" % lang)
        try:
            tables[lang] = json.load(open(p, encoding="utf-8"))
        except (OSError, ValueError) as e:
            errors.append("%s: %s" % (p, e))
    en = tables.get("en", {})
    ops = set()
    for p in glob.glob(os.path.join(ROOT, "editor", "*.c")):
        ops |= set(OP.findall(open(p, encoding="utf-8").read()))
    used = set()
    for p in glob.glob(os.path.join(ROOT, "editor", "*.c")):
        used |= set(KEY.findall(open(p, encoding="utf-8").read()))
    used -= ops - set(en)  # operation names share the dotted style; they are not keys
    for k in sorted(used - set(en)):
        errors.append("key used in code but missing from English: %s" % k)
    for lang, t in tables.items():
        if lang == "en":
            continue
        for k in sorted(set(en) - set(t)):
            errors.append("%s: missing %s" % (lang, k))
        for k in sorted(set(t) - set(en)):
            errors.append("%s: extra key %s" % (lang, k))
        for k, v in t.items():
            if k in en and conversions(v) != conversions(en[k]):
                errors.append("%s: %s changes the format (%r vs %r)" % (lang, k, v, en[k]))
    unused = sorted(set(en) - used)
    for e in errors:
        print("i18n: " + e)
    if unused:
        print("i18n: note: %d keys not referenced by the editor: %s" % (len(unused), ", ".join(unused)))
    if errors:
        return 1
    print("i18n check OK (%d keys, %d languages)" % (len(en), len(tables)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
