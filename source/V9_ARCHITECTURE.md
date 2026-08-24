# SCM AutoContour V9.1.1 production architecture

The CorelDRAW command calls `buildManufacturingPaths`; it does not call the old
polyline planner directly.

| Stage | Production implementation |
|---|---|
| Image segmentation | OpenCV-assisted RGBA/white-background product extraction |
| Edge location | Three-scale Gaussian/Scharr normal search with parabolic subpixel refinement |
| Straight geometry | Deterministic RANSAC followed by Eigen TLS/PCA refit |
| Circular geometry | Robust IRLS circle and circular-arc fitting |
| Elliptic geometry | Robust nonlinear ellipse/elliptic-arc fitting |
| Corners | Three-scale persistent corner detector |
| Primitive planning | Sparse global shortest-path MDL planner with fixed model-description cost |
| Irregular curves | Adaptive cubic Bezier or clamped cubic B-spline span; no polyline tessellation in CorelDRAW |
| Global continuity | One Ceres problem for every shared primitive junction with robust losses |
| Topology | Boost.Geometry simplicity check plus explicit closure/duplicate checks |
| Offset | Exact analytic offset for circles and line polygons; Clipper2 round-join fallback for mixed curves |
| Verification | Floating-point bidirectional boundary distance; strict supported-edge p95/Hausdorff plus an independent bounded LINE/LINE corner-reconstruction gate |
| CorelDRAW output | One LINE per accepted straight run; analytic circles/arcs/ellipses converted to cubic curve spans; editable closed CPG curve |

## Manufacturing invariants

- A complete straight run that passes tolerance wins before arc/Bezier models.
- A full round hole that passes tolerance is one `CIRCLE` primitive.
- An accepted path must be closed, simple, and free of duplicate segments.
- A sharp corner reconstructed from two supported straight runs is checked by
  its own extension-length and angle gate; it cannot relax the ordinary edge
  tolerance or hide a bad curve.
- The installer builds x64 C++20 Release, runs core and production tests, then
  installs only if all tests pass.
- Exact fitting cannot recover detail that is absent from the source pixels;
  reported precision is always the measured deviation from the extracted edge.
