# SCM AutoContour V9.1.0

Native 64-bit CorelDRAW 2024 CPG plugin for manufacturing/laser-cutting contours.

## One-click installation

1. Extract the entire ZIP.
2. Close CorelDRAW.
3. Double-click the single installer in the ZIP root.
4. Allow the administrator prompt. The first build downloads the official
   open-source dependencies and can take several minutes.
5. The installer does not copy the plugin unless both engine tests and the full
   production-pipeline tests pass.

The installer automatically detects CorelDRAW 2024, Visual Studio 2022 C++ and
the CMake shipped with Visual Studio. Dependency builds are cached under the
current Windows user profile, so later installs are faster.

## Required Visual Studio components

- Desktop development with C++
- MSVC v143 x64/x86 build tools
- Windows 10 or Windows 11 SDK
- C++ CMake tools for Windows

## Output rules

- straight run -> one LINE;
- round hole -> one CIRCLE;
- smooth circular/elliptic boundary -> ARC/ELLIPTIC_ARC;
- irregular boundary -> adaptive cubic Bezier/B-spline;
- open, self-intersecting, duplicate, or out-of-tolerance paths are rejected
  before being written to the CorelDRAW document.

See `V9_ARCHITECTURE.md` for the implemented pipeline and
`THIRD_PARTY_NOTICES.txt` for dependency licensing.
