#pragma once

#include <mutex>

#include "RE/N/NiMatrix3.h"
#include "RE/N/NiSmartPointer.h"
#include "RE/T/TESCamera.h"

namespace RE
{
	class Actor;
	class NiAVObject;
	class NiNode;
	class NiPoint3;
	class TESObjectREFR;
}

namespace NPCIC
{
	// Owns the PlayerCamera::Update call-site hook and the live
	// "view this NPC" state.
	//
	// Mechanism (modeled after Cinematic-Mode): patch an internal call site
	// inside PlayerCamera::Update (REL::RelocationID 49852 SE / 50784 AE)
	// with a trampoline write_call<5>. The thunk lets the original run first
	// (so the engine still derives rotation from mouse look and updates the
	// camera state), then directly overwrites the cameraRoot NiNode's local
	// transform so its world translate matches the target NPC's head, and
	// calls Update to propagate. Previous attempts hooked
	// ThirdPersonState::GetTranslation/Update vtables but those were not
	// invoked for the camera render position.
	class NPCCamera
	{
	public:
		static NPCCamera& Get() { static NPCCamera instance; return instance; }

		bool IsAttached() const { return attached; }

		// Begin viewing from a_target's head. Forces third-person so the
		// ThirdPersonState path is active, and (optionally) shrinks the
		// NPC's head node so it does not occlude the camera.
		void Attach(RE::Actor* a_target);

		// Stop viewing and restore the target's head scale if it was hidden.
		void Detach();

		// Safety restore on save load / new game.
		void RestoreState() { Detach(); }

		// Installs the PlayerCamera::Update call-site trampoline hook.
		static void Install();

		// Hook thunk.
		static void CameraUpdateThunk(RE::TESCamera* a_camera);

		// When the crosshair has no valid NPC, search a_radius units around
		// a_player for the nearest living NPC (skips the player and dead
		// actors). Returns null if none in range. Uses ProcessLists::ForAllActors.
		static RE::Actor* FindNearestNPC(RE::Actor* a_player, float a_radius);

	private:
		NPCCamera() = default;

		RE::NiAVObject* GetHeadNode(RE::Actor* a_actor) const;
		void            HideHead(RE::Actor* a_actor, bool a_hide);
		void            ApplyCameraTransform(RE::TESCamera* a_camera);

		// Save the current world FOV and, if Settings::NpcViewFOV > 0, override
		// it. Called from Attach.
		void            ApplyFOV();
		// Restore the saved world FOV if it was overridden. Called from Detach.
		void            RestoreFOV();

		std::mutex                       lock;
		bool                            attached{ false };
		RE::NiPointer<RE::TESObjectREFR> target;
		float                           savedHeadScale{ 1.0f };
		float                           savedWorldFOV{ 0.0f };
		bool                            fovOverridden{ false };

		// Rotation-tracking state (per CameraRotationMode INI setting).
		//
		// We work with pure heading/pitch Euler deltas extracted from the
		// engine forward vector each frame -- independent of the engine's
		// internal Euler composition order.
		//
		// Heading = yaw around world Z, radians, atan2(fwd.y, fwd.x).
		// Pitch   = look up/down,  radians, asin(clamp(fwd.z, -1, 1)).
		//
		// Sign conventions:
		//   * Pitch: engine reports "push mouse forward = decreasing fwd.z"
		//     while player expects "push forward = look up = +fwd.z", so
		//     pitch engine delta is always NEGATED on input.
		//   * Yaw:   engine reports "drag mouse left = decreasing heading"
		//     while player expects "drag left = view turns LEFT = more
		//     leftward heading in world terms", so yaw engine delta is
		//     also NEGATED on input.
		//
		// In both modes mouse deltas are computed RELATIVE TO A BASELINE,
		// not as a running frame-to-frame accumulation, to avoid numeric
		// drift and idle-camera smoothing jitter.
		//
		// Mode 1 (Always Head + Mouse Offset):
		//   Baseline engine angles are captured on the first frame after
		//   Attach. Each frame the total signed mouse delta since Attach
		//   is:  dH = -(currentEngH - attachEngH)
		//          dP = -(currentEngP - attachEngP)
		//   final = Rz(dH) * currentHead.world.rotate * Rx(dP)
		//   => the camera CONTINUOUSLY tracks the current head orientation
		//      AND layers the player's total mouse rotation on top. Even if
		//      the NPC turns its head 30 degrees the camera follows it,
		//      while still preserving the player's +-mouse offset from the
		//      "current head facing". Head POSITION is also tracked.
		//
		// Mode 2 (Snap-then-Handoff, default):
		//   While snapActive=true: final = currentHead.world.rotate (snap
		//   to head, first sight is exactly what the NPC sees).
		//   At handoff (first real mouse movement past grace):
		//     * freeze handoffEngineHeading/Pitch (baseline angles)
		//     * freeze handoffHeadRotate = current head world matrix
		//       (this is the "starting angle" the user refers to; using
		//       the full matrix preserves head roll/nodding at handoff)
		//   After handoff:
		//     dH = -(currentEngH - handoffEngH)
		//     dP = -(currentEngP - handoffEngP)
		//     final = Rz(dH) * handoffHeadRotate * Rx(dP)
		//   => pure mouse control starting from the exact handoff
		//      orientation, continuous at handoff, correct signs. Head
		//      POSITION anchoring still tracks the current head node each
		//      frame so the camera does not drift away when the NPC moves.
		bool          haveAttachBaseline{ false };
		float         attachEngineHeading{ 0.0f };  // Mode 1, radians
		float         attachEnginePitch{ 0.0f };    // Mode 1, radians

		// Mode 2 state
		bool          snapActive{ false };
		std::uint32_t graceFrames{ 0 };
		bool          haveLastEngineAngles{ false };  // mode-2 snap handoff detection
		float         lastEngineHeading{ 0.0f };      // mode-2 only
		float         lastEnginePitch{ 0.0f };        // mode-2 only
		bool          handoffDone{ false };
		float         handoffEngineHeading{ 0.0f };   // radians
		float         handoffEnginePitch{ 0.0f };     // radians
		RE::NiMatrix3 handoffHeadRotate{};            // frozen at handoff
	};
}
