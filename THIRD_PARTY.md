# Third-party components

MEXNya is licensed under the GNU General Public License v3.0 (see `LICENSE`). It
includes or downloads the following components, each under its own licence, which
is compatible with distribution alongside GPL-3.0 code.

| Component | Where | Licence | Notice |
|---|---|---|---|
| **SDL 3** (window, input, audio, OpenGL context) | downloaded at configure time by CMake (or an installed SDL3) | zlib | https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt |
| **Lua 5.4** (scripting) | `third_party/lua/` | MIT | `third_party/lua/LICENSE` |
| **minimp3** (MP3 decoding) | `engine/audio/minimp3/` | CC0 1.0 | `engine/audio/minimp3/LICENSE` |
| **Fusion Pixel Font** 12 px by TakWolf, baked into `mx12.mxf` | `engine/resources/fonts/` | SIL Open Font License 1.1 | `engine/resources/fonts/OFL.txt`, `engine/resources/fonts/LICENSES/` |

Fusion Pixel Font is built on Ark Pixel Font (TakWolf), Cubic 11 (including glyphs
from JF Dot M+H 12 by M+ FONTS PROJECT and M+ BITMAP FONTS by COZ) and Galmuri (Lee
Minseo); their notices are in `engine/resources/fonts/LICENSES/`. The baked file uses
the generic name `mx12` and none of the reserved font names. Under the OFL the font
data stays under the OFL; it does not become GPL, and the OFL does not extend to the
engine's code.

The optional *system font* mode draws with fonts installed on the user's computer
(CoreText on macOS, GDI on Windows). No system font is copied into this repository or
into anything the engine builds.
