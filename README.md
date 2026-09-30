# C4D ShrinkWrap

A high-performance, multi-threaded ShrinkWrap Deformer plugin for Maxon Cinema 4D (2025 and 2026) built with C++ and custom Triangle BVH acceleration.

Designed for both traditional surface projection and high-speed live retopology workflows (Maya QuadDraw-style).

---

## Features

- **Multi-Threaded BVH Acceleration**: Fast raycasting and nearest-surface queries across all CPU cores via Maxon ParallelFor.
- **Hierarchy and Generator Support**: Snap to complex target hierarchies (Nulls, Connect, Boole, Symmetry) and live Subdivision Surface caches.
- **QuadDraw-Style Retopo Display**: 
  - Semi-transparent tinted mesh overlay (custom color and opacity).
  - Clean, crisp anti-aliased wireframe overlay with zero Z-fighting.
- **Live Deformed Editing and Auto-Bake**:
  - Edit vertices directly in their snapped positions in the viewport.
  - Optional Live Auto-Bake directly to the base mesh or one-click "Apply to Mesh".
- **4 Projection Modes**:
  - Nearest Surface (closest point projection)
  - Project (along vertex normals, bidirectional)
  - Nearest Vertex
  - Target Normal
- **Advanced Controls**: Offset, Strength, Falloff Radius, "Above Surface Only", "Snap to Vertices & Edges" (toggle between uniform surface sliding and vertex/edge attraction), and Vertex Map weight masking.
- **Stability**: Fully undo-safe (Ctrl+Z), thread-safe evaluation, zero memory leaks.

---

## Installation

1. Download the latest release from the Releases section.
2. Unpack the `C4D_ShrinkWrap` folder into your Cinema 4D plugins directory:
   - **Windows**: `C:\Users\<User>\AppData\Roaming\Maxon\Maxon Cinema 4D <Version>\plugins\`
3. Restart Cinema 4D.
4. Access via **Extensions / Tools -> ShrinkWrap** or from the Deformers menu.

---

## Building from Source

- **Requirements**: Visual Studio 2022 (C++20), CMake 3.30+, Cinema 4D SDK 2025 / 2026.
- Run `build_2026.bat` or `build_2025.bat`.
