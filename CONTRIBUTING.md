# Contributing to MEXNya

Thanks for helping. MEXNya is plain C99 with CMake; the engine, the editor, the
samples and the tests live in one repository and build together.

## Build and test

```bash
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug -j
bash scripts/test.sh
```

The first configure downloads the pinned SDL3 release. Pass
`-DMEXNYA_SDL3_DIR=/path/to/SDL` to use an existing source tree, or install SDL3
where `find_package(SDL3)` finds it. See the [build reference](docs/guide.md#build-tool-and-mexnyajson)
for every option.

`scripts/test.sh` must pass unchanged. It runs the dependency-boundary check, the
interface-string check, the runtime tests, a headless smoke test of the engine and
the sample game, and the editor operation layer.

## Code

- C99 only. No C11 features, no C++.
- The build must stay warning-free with `-Wall -Wextra`.
- Public engine headers (`engine/**/*.h`) must not include or expose SDL, OpenGL or
  Lua types. `scripts/check_boundaries.py` enforces this.
- Four spaces, no tabs, braces on the same line, `s_` for file-local state and module
  prefixes (`json_`, `plat_`, `r_`, `ui_`, `font_`, `mx_`) for the public API.
- Handle every fallible call; never use `strcpy`, `strcat` or `sprintf` on user data.
  Write project files, saves and logs through the atomic write helper.
- Do not modify `third_party/` and do not add dependencies without a note in
  `THIRD_PARTY.md`.

## Interface text

The editor's interface is translated into English, Simplified and Traditional
Chinese, Japanese and Korean. A new or changed string must be added to
`engine/resources/lang/editor.en.json` and the other four files.
`scripts/check_i18n.py` verifies the key set and the printf conversions.

## Commits and pull requests

Use one commit per topic and an English message of the form
`area: lowercase summary`, for example
`engine: add fixed-timestep loop with render interpolation`. Keep unrelated
reformatting out of the change. Describe what was verified (commands and results)
in the pull request.

## Licence

By contributing you agree that your work is distributed under the GNU General
Public License v3.0 (see [LICENSE](LICENSE)). There is no contributor licence
agreement.
