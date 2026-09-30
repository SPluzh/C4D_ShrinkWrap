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
- **Advanced Controls**: Offset, Strength, Falloff Radius, "Above Surface Only", "Snap to Vertices & Edges", "Ignore Camera (Target Normal)", Vertex Map weight masking, and **Exclude Selection Tag** (Point, Edge, or Polygon selection tags to lock components from deformation).
- **Stability**: Fully undo-safe (Ctrl+Z), thread-safe evaluation, zero memory leaks.

---

## Masking & Restriction Controls

### Vertex Map Restriction
- **Smooth Influence Control**: Drag a **Vertex Map Tag** into this field to modulate the projection strength per vertex with smooth floating-point weights (0.0 to 1.0).
- **Field System Integration**: Fully compatible with Cinema 4D Fields (Box, Spherical, Random, Decay, Spline Fields) driving the Vertex Map for dynamic, procedural falloff.
- **Soft Transitions**: Ideal for smoothly blending between deformed and non-deformed areas of your mesh.

### Exclude Selection Tag
- **Hard Component Locking**: Drag a **Point Selection Tag**, **Edge Selection Tag**, or **Polygon Selection Tag** (or an exclusion Vertex Map) to completely lock chosen parts from snapping.
- **Retains Original Positions**: Excluded components are completely skipped during calculation and remain at their base un-deformed coordinates, even when **Live Auto-Bake** is enabled.

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
