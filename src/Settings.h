#pragma once

#include "REX/REX/INI.h"

// NPC IC settings. Backed by REX::INI; the file is NPCIC.ini next to the DLL
// (Data/SKSE/Plugins/NPCIC.ini). Missing keys fall back to the defaults here.
namespace NPCIC::Settings
{
	void Load();

	// Win32 virtual-key code of the hotkey that toggles NPC view.
	// Default 0x75 = VK_F6. Examples: F1=0x70 F4=0x73 F7=0x76 F8=0x77 F9=0x78
	inline REX::INI::U32 ToggleHotKey{ "General", "iToggleHotKey", 0x75 };

	// Shrink the target NPC's "NPC Head [Head]" node while viewing so the head
	// does not block the camera. 1 = hide, 0 = do not hide.
	inline REX::INI::Bool HideNpcHead{ "General", "bHideNpcHead", true };

	// Verbose per-frame / per-key debug logging (heartbeats, key codes,
	// crosshair RTTI, hook-first-invocation). Set to 0 once tuning is done
	// to silence the log; load/attach/detach lines always print regardless.
	inline REX::INI::Bool Debug{ "General", "bDebug", true };

	// Camera offset relative to the NPC's Head node, applied in VIEW space
	// (rotated by the mouse-driven view direction): +X right, +Y forward,
	// +Z up. Start at 0,0,0 and tune so the camera sits at eye level.
	// NOTE: originally these lived in a separate [Camera] section, but some
	// REX::INI builds fail silently when a second section header is
	// encountered (a user deploy showed the entire second section falling
	// to C++ defaults while [General] loaded fine). Keeping every key in
	// [General] is the bulletproof workaround.
	inline REX::INI::F32 CameraOffsetX{ "General", "fCameraOffsetX", 0.0f };
	inline REX::INI::F32 CameraOffsetY{ "General", "fCameraOffsetY", 0.0f };
	inline REX::INI::F32 CameraOffsetZ{ "General", "fCameraOffsetZ", 0.0f };

	// Field of view (in degrees) to apply while in NPC view. The vanilla world
	// FOV is saved on Attach and restored on Detach. <=0 means do not change
	// FOV (use the game's current value). Typical range 65-90.
	inline REX::INI::F32 NpcViewFOV{ "General", "fNpcViewFOV", 0.0f };

	// When the crosshair has no valid NPC target on hotkey press, search the
	// player's surroundings for the nearest NPC and attach to it instead.
	// This is the search radius in Skyrim units (1 unit ~= 1.4 cm); 50 ~= 70 cm.
	// <=0 disables auto-targeting (only the crosshair target is used).
	inline REX::INI::F32 AutoTargetRadius{ "General", "fAutoTargetRadius", 50.0f };

	// How to handle camera rotation while viewing an NPC.
	// 1 (Always Head + Mouse Offset): the base rotation always follows the NPC
	//   head node's current WORLD rotation (snap-like but permanent). Moving
	//   the mouse adds an accumulative ANGULAR OFFSET on top of the live head
	//   orientation, so if the NPC turns its head (animation, walking, etc.)
	//   your view turns with it PLUS whatever mouse offset you have dialed in.
	//   When the mouse is perfectly still the view exactly equals the NPC's
	//   current facing -- identical to the "snap" phase of mode 2, but never
	//   hands off.
	// 2 (Snap-then-Handoff, default): entering NPC view initially snaps the
	//   camera rotation to the NPC head (so the first sight is what the NPC
	//   sees); once the player moves the mouse, rotation is handed off to pure
	//   mouse control seamlessly (the handoff preserves the head-facing angle
	//   at that instant as the starting point, so there is no abrupt jump back
	//   to the player's old camera angle). Head POSITION anchoring stays active
	//   in both modes.
	inline REX::INI::U32 CameraRotationMode{ "General", "iCameraRotationMode", 2u };
}
