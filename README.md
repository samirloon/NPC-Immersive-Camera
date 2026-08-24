# NPC IC - NPC Immersive Camera

**NPC IC** (NPC Immersive Camera) is an SKSE plugin for The Elder Scrolls V: Skyrim Special Edition and Anniversary Edition. It allows you to "possess" any selected NPC's viewpoint with a hotkey, seeing the world through their eyes, and switch between two different camera control modes.

## Main Features
*   **Toggle Viewpoint**: Aim at any NPC in-game and press the hotkey (default F6) to attach the camera to that NPC's head position.
*   **Dual Control Modes**:
    *   **Mode 1 (Always Follow)**: The camera orientation continuously follows the NPC head's real-time rotation, while you can still accumulate additional angular offset with the mouse.
    *   **Mode 2 (Snap then Handoff, Default)**: Upon entering the viewpoint, the camera first snaps to the NPC's facing direction. When you move the mouse, control smoothly and seamlessly hands off entirely to you for unrestricted mouse look.
*   **Configurable**: Through the `Data/SKSE/Plugins/NPCIC.ini` file, you can adjust the hotkey, whether to hide the NPC's head, camera offset, Field of View (FOV), auto-target radius, and camera control mode.
*   **Auto Target**: If the crosshair isn't pointing at any NPC, you can set a search radius to automatically lock onto the nearest NPC.
*   **Compatibility**: Built on `CommonLibSSE-NG`, a single DLL supports Skyrim SE, AE.

## Requirements
*   **Skyrim SE/AE**
*   **[SKSE64](https://skse.silverlock.org/)**
*   **[Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)** (SE/AE)

## Installation
1.  Place the `NPCIC.dll` file into the `Data/SKSE/Plugins/` folder.
2.  (Optional) After running the game once, the `Data/SKSE/Plugins/NPCIC.ini` configuration file will be generated automatically (or you can create it manually). Adjust the parameters as needed.
3.  Launch the game via the SKSE loader.

## Compatibility
*   **Compatible with Improve Camera**
*   **Compatible with SmoothCam**

### Environment
*   **Visual Studio 2022** (or 2026): During installation, select the "Desktop development with C++" workload.
*   **CMake** (version 3.19 or higher).
*   **vcpkg**: It is recommended to install it via the Visual Studio installer, or set the `VCPKG_ROOT` environment variable.
*   **This project is built on `CommonLibSSE-NG`.**

## Explanation
**Project Development Description**: During the process of code writing, debugging, and documentation generation for this project, a large language model (AI) was utilized as an auxiliary development tool.

### License
*   **GPL-3.0**
