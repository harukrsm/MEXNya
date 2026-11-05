# Security policy

## Supported versions

MEXNya is at 0.1.0. Security fixes land on `main`; older tags are not maintained.

## Reporting a vulnerability

Please do not open a public issue for a vulnerability. Use GitHub's private
vulnerability reporting on this repository
(**Security** → **Report a vulnerability**), or contact the maintainer through
the address on their GitHub profile.

Include the affected commit or version, the platform, reproduction steps, and what an
attacker could achieve. We aim to acknowledge a report within seven days and to
publish a fix and an advisory once a patch is available.

## Scope

MEXNya loads project files (JSON scenes, PNG images, Lua scripts, plugin libraries)
and opens a window, an OpenGL context and an audio device. Treat projects received
from other people as untrusted input. The engine runs Lua without the `os`, `io`,
`package` and `debug` libraries, but native plugins are code execution by design.
