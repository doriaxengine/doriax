<p align="center">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/logo/doriax_logo_transparent.png" alt="Doriax Engine" width="300">
</p>

<p align="center">
  A free, open-source 2D/3D game engine and integrated editor for building cross-platform games and interactive projects with Lua or C++.
</p>

<p align="center">
  <strong>Doriax Engine</strong> is the continuation of Supernova Engine under a new identity and a broader workflow focused on visual editing, shader generation, and project export.
</p>

<p align="center">
  <a href="https://github.com/supernovaengine/supernova/actions/workflows/cmake.yml"><img src="https://github.com/supernovaengine/supernova/actions/workflows/cmake.yml/badge.svg?branch=main" alt="Editor Desktop"></a>
  <a href="https://github.com/supernovaengine/supernova/actions/workflows/engine-cmake.yaml"><img src="https://github.com/supernovaengine/supernova/actions/workflows/engine-cmake.yaml/badge.svg?branch=main" alt="Engine Desktop"></a>
  <a href="https://github.com/supernovaengine/supernova/actions/workflows/engine-android.yml"><img src="https://github.com/supernovaengine/supernova/actions/workflows/engine-android.yml/badge.svg?branch=main" alt="Engine Android"></a>
  <a href="https://github.com/supernovaengine/supernova/actions/workflows/engine-emscripten.yaml"><img src="https://github.com/supernovaengine/supernova/actions/workflows/engine-emscripten.yaml/badge.svg?branch=main" alt="Engine Emscripten"></a>
  <a href="https://github.com/supernovaengine/supernova/actions/workflows/engine-xcode.yaml"><img src="https://github.com/supernovaengine/supernova/actions/workflows/engine-xcode.yaml/badge.svg?branch=main" alt="Engine iOS/macOS"></a>
  <a href="https://discord.gg/yXXDyJf3gT"><img src="https://img.shields.io/discord/1356958061880934480?label=Discord&logo=discord&style=flat&color=5865F2" alt="Join our Discord"></a>
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/screenshots/editor-3d-scene.png" alt="Doriax Engine Editor">
</p>

## Overview

Doriax Engine combines a lightweight runtime with a modern desktop editor for creating 2D and 3D games. The project keeps its ECS and data-oriented foundation, but now extends far beyond a runtime API: author scenes visually, script in Lua or C++, generate shader data, export projects, and build for multiple platforms from a single environment.

## Why Doriax

- Integrated editor for 2D and 3D scene creation
- Project generation, build orchestration, and export tooling
- Shader compilation and cross-platform shader translation for exported projects
- Lua for fast iteration and C++ for native performance
- ECS-based, data-oriented runtime designed for efficiency
- Cross-platform deployment with OpenGL, Metal, and DirectX backends

## Editor Workflow

- Scene hierarchy, inspector, and resource management
- 2D tools for sprites, tilemaps, and sprite slicing
- 3D scene editing with cameras, lighting, models, and play mode
- Animation tools, timeline editing, and bone workflows
- Integrated code editor and scripting workflow for Lua and C++
- Shader-aware export pipeline that prepares scenes, assets, scripts, engine files, and compiled shaders

> **Note:** Terrain authoring, particle editing, audio tooling, and direct shader manipulation are not fully integrated into the editor yet — these features are available at the engine/runtime level.

<p align="center">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/screenshots/editor-2d-tilemap.png" alt="Doriax Engine 2D tilemap editor" width="48%">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/screenshots/editor-animation.png" alt="Doriax Engine animation timeline" width="48%">
</p>
<p align="center">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/screenshots/editor-code.png" alt="Doriax Engine integrated code editor" width="48%">
  <img src="https://raw.githubusercontent.com/doriaxengine/doriax-site/main/screenshots/editor-ui.png" alt="Doriax Engine UI tools" width="48%">
</p>

## Engine Features

- 2D and 3D scenes
- GLTF and OBJ model loading
- Skeletal animation and morph targets
- PBR materials, dynamic shadows, fog, and sky
- Particle systems, UI, terrain LOD, and instancing
- 3D audio, scene serialization, texture and shader pools, and multithreading
- Integrated 2D and 3D physics powered by Box2D and Jolt Physics

## Platforms

| Area | Support |
| --- | --- |
| Editor downloads | Windows, Linux, macOS |
| Project targets | Windows, Linux, macOS, Android, iOS, HTML5 |
| Graphics APIs | OpenGL, Metal, DirectX |
| Scripting | Lua, C++ |

## Getting Started

Download a prebuilt editor from [doriaxengine.org](https://doriaxengine.org/#download) or build from source:

```bash
git clone https://github.com/doriaxengine/doriax.git
cd doriax
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The root project builds `doriax-editor`. On single-config generators the executable is typically created under `build/`. On multi-config generators such as Visual Studio, look under the configuration subdirectory. Platform-specific setup is still being refreshed under the Doriax name.

## Documentation and Community

- [Website](https://doriaxengine.org)
- [Discord](https://discord.gg/yXXDyJf3gT)

## Repository Layout

- `editor/` - desktop editor, windows, tools, project generation, and export flow
- `engine/` - runtime engine, platform layers, rendering, ECS, and project templates
- `shadercompiler/` - shader compilation and translation tools
- `libs/` - bundled third-party dependencies

## Transition from Supernova

Doriax Engine is the next phase of Supernova Engine. Version `0.5.5` was the last release of the legacy Supernova engine. Some internal folders, and older external references may still mention the previous name while the rebranding and documentation refresh continue.

## License

Doriax Engine is released under the MIT License and is free for personal and commercial use. Third-party libraries included in the repository keep their own licenses.
