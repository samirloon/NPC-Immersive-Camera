#include "NPCCamera.h"

#include "Settings.h"

#include "RE/Skyrim.h"  // umbrella: Actor, NiAVObject, NiPoint3, NiMatrix3, ...
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"  // SKSE::log, SKSE::AllocTrampoline, SKSE::GetTrampoline

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace NPCIC
{
	namespace
	{
		// Storage for the original function that PlayerCamera::Update calls
		// at the patched call site (REL::RelocationID(49852, 50784) + 0x1A6).
		// The trampoline.write_call<5> returns the original function pointer,
		// which we invoke first inside the thunk before overriding the camera.
		REL::Relocation<decltype(NPCCamera::CameraUpdateThunk)> g_origCameraUpdate;

		// Returns true if the two rotation matrices differ by more than a
		// small epsilon in any entry. Used to detect that the player moved
		// the mouse (the engine recomputed the camera rotation) so we can
		// release the initial NPC-head-facing snap. A real mouse-drag at 60
		// FPS changes entries by far more than epsilon; smoothing jitter and
		// recomputation noise stay below it.
		bool RotationChanged(const RE::NiMatrix3& a, const RE::NiMatrix3& b)
		{
			constexpr float kEps = 5e-3f;  // ~= 0.29 deg on a direction cosine
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					if (std::fabs(a.entry[i][j] - b.entry[i][j]) > kEps) {
						return true;
					}
				}
			}
			return false;
		}

		// Extract the heading (yaw around world Z) and pitch (look up/down)
		// from a world rotation matrix. This is independent of the engine's
		// Euler composition order because we read the actual forward vector.
		// Skyrim convention: +X = right, +Y = forward, +Z = up.
		// Output angles are in radians.
		void ExtractHeadingPitch(const RE::NiMatrix3& a_rotate,
			float& a_outHeading, float& a_outPitch)
		{
			const float fx = a_rotate.entry[0][1];  // +Y column: forward.x
			const float fy = a_rotate.entry[1][1];  // forward.y
			const float fz = a_rotate.entry[2][1];  // forward.z

			a_outHeading = std::atan2(fy, fx);

			// Clamp to [-1, 1] to avoid NaN from asin due to floating drift.
			const float fzClamped = std::clamp(fz, -1.0f, 1.0f);
			a_outPitch = std::asin(fzClamped);
		}

		// Shortest-path signed angle difference from a_from to a_to, both
		// in radians, wrapping correctly around +/-pi.
		float AngleDiff(float a_to, float a_from)
		{
			float d = a_to - a_from;
			while (d >  3.14159265358979f) d -= 6.28318530717959f;
			while (d < -3.14159265358979f) d += 6.28318530717959f;
			return d;
		}

		// Compose a final rotation that takes a_base rotation (e.g. the NPC
		// head world matrix at some reference moment) and applies a global
		// yaw offset (around world Z) followed by a local pitch offset
		// (around the post-yaw local X axis).
		//   result = Rz(yawRad) * a_base * Rx(pitchRad)
		// This is the standard first-person composition: yaw changes the
		// overall heading, pitch is relative to the camera's right axis.
		RE::NiMatrix3 ComposeYawPitchOffset(const RE::NiMatrix3& a_base,
			float a_yawRad, float a_pitchRad)
		{
			RE::NiMatrix3 rz;
			rz.MakeZRotation(a_yawRad);
			RE::NiMatrix3 rx;
			rx.MakeXRotation(a_pitchRad);
			return rz * a_base * rx;
		}
	}

	RE::NiAVObject* NPCCamera::GetHeadNode(RE::Actor* a_actor) const
	{
		if (!a_actor) return nullptr;
		auto* root = a_actor->GetCurrent3D();
		if (!root) return nullptr;
		// Function-local static: constructed on first use (in-game), so the
		// BSFixedString pool is guaranteed initialized.
		static const RE::BSFixedString headName{ "NPC Head [Head]" };
		return root->GetObjectByName(headName);
	}

	void NPCCamera::HideHead(RE::Actor* a_actor, bool a_hide)
	{
		auto* head = GetHeadNode(a_actor);
		if (!head) return;

		if (a_hide) {
			savedHeadScale = head->local.scale;
			head->local.scale = 0.001f;  // shrink to near-invisibility
		}
		else {
			if (savedHeadScale > 0.0f && savedHeadScale < 1000.0f) {
				head->local.scale = savedHeadScale;
			}
			savedHeadScale = 1.0f;
		}
		// The actor's 3D update each frame recomputes world from local, so the
		// scale change propagates on the next render. (Origin is unaffected by
		// scale, so the camera anchor position stays valid.)
	}

	void NPCCamera::Attach(RE::Actor* a_target)
	{
		if (!a_target) {
			SKSE::log::warn("Attach called with null target");
			return;
		}
		SKSE::log::info("Attach: target formID=0x{:08X}",
			a_target->GetFormID());

		std::lock_guard<std::mutex> guard(lock);
		if (attached) {
			SKSE::log::info("Attach: already attached, ignoring");
			return;
		}

		// NiPointer's ctor is explicit, so use reset() to take the strong ref
		// (TryAttach/IncRef handled by reset; released on Detach).
		target.reset(a_target);
		savedHeadScale = 1.0f;

		auto* head = GetHeadNode(a_target);
		SKSE::log::info("Attach: head node found={}", head != nullptr);

		if (Settings::HideNpcHead.GetValue() && head) {
			HideHead(a_target, true);
		}
		attached = true;
		SKSE::log::info("Attach: attached=true, HideNpcHead={}",
			Settings::HideNpcHead.GetValue());

		// Initialize rotation-tracking state according to the configured
		// CameraRotationMode. Both modes start with the camera facing the
		// same direction as the NPC head (so "what the NPC sees" is what
		// the player sees on frame 1). Engine heading/pitch baselines are
		// captured lazily on the first ApplyCameraTransform call after
		// Attach, because the engine may still be settling the camera
		// state during ForceThirdPerson.
		{
			haveAttachBaseline = false;
			attachEngineHeading = 0.0f;
			attachEnginePitch   = 0.0f;

			snapActive = false;
			graceFrames = 0;
			haveLastEngineAngles = false;
			lastEngineHeading = 0.0f;
			lastEnginePitch   = 0.0f;

			handoffDone = false;
			handoffEngineHeading = 0.0f;
			handoffEnginePitch   = 0.0f;
			handoffHeadRotate = RE::NiMatrix3{};

			const auto mode = Settings::CameraRotationMode.GetValue();
			if (mode == 1u) {
				SKSE::log::info("Attach: rotation mode 1 (always head + mouse offset baseline)");
			}
			else {
				// Mode 2 (default): start in the snap phase. The camera
				// will inherit head orientation directly each frame until
				// the first real mouse input is detected past the grace.
				snapActive = true;
				graceFrames = 5;
				SKSE::log::info("Attach: rotation mode 2 (snap + handoff), graceFrames={}",
					graceFrames);
			}
		}

		// Apply FOV override after attaching so the projection picks it up
		// on the next PlayerCamera::Update (the call-site hook runs there
		// every frame, but FOV is read separately for the view matrix).
		ApplyFOV();

		// The hook only fires while ThirdPersonState is the active camera state.
		if (auto* pc = RE::PlayerCamera::GetSingleton()) {
			pc->ForceThirdPerson();
			SKSE::log::info("Attach: ForceThirdPerson called");
		}
		else {
			SKSE::log::warn("Attach: PlayerCamera singleton is null");
		}
	}

	void NPCCamera::Detach()
	{
		std::lock_guard<std::mutex> guard(lock);
		if (!attached) {
			RestoreFOV();
			target.reset();
			savedHeadScale = 1.0f;
			haveAttachBaseline = false;
			attachEngineHeading = 0.0f;
			attachEnginePitch   = 0.0f;
			snapActive = false;
			graceFrames = 0;
			haveLastEngineAngles = false;
			lastEngineHeading = 0.0f;
			lastEnginePitch   = 0.0f;
			handoffDone = false;
			handoffEngineHeading = 0.0f;
			handoffEnginePitch   = 0.0f;
			handoffHeadRotate = RE::NiMatrix3{};
			return;
		}

		RestoreFOV();

		if (Settings::HideNpcHead.GetValue()) {
			if (auto* actor = static_cast<RE::Actor*>(target.get())) {
				HideHead(actor, false);
			}
		}
		target.reset();
		savedHeadScale = 1.0f;
		attached = false;
		haveAttachBaseline = false;
		attachEngineHeading = 0.0f;
		attachEnginePitch   = 0.0f;
		snapActive = false;
		graceFrames = 0;
		haveLastEngineAngles = false;
		lastEngineHeading = 0.0f;
		lastEnginePitch   = 0.0f;
		handoffDone = false;
		handoffEngineHeading = 0.0f;
		handoffEnginePitch   = 0.0f;
		handoffHeadRotate = RE::NiMatrix3{};
	}

	void NPCCamera::ApplyFOV()
	{
		// Save the live world FOV, then (if configured) write the override.
		// The engine reads PlayerCamera::worldFOV each frame for the view
		// projection; there is no engine setter, so write the field directly.
		auto* pc = RE::PlayerCamera::GetSingleton();
		if (!pc) {
			SKSE::log::warn("ApplyFOV: PlayerCamera singleton is null");
			return;
		}
		savedWorldFOV = pc->GetRuntimeData2().worldFOV;
		fovOverridden = false;

		const float desired = Settings::NpcViewFOV.GetValue();
		if (desired > 0.0f) {
			pc->GetRuntimeData2().worldFOV = desired;
			fovOverridden = true;
			SKSE::log::info("ApplyFOV: worldFOV {} -> {}", savedWorldFOV, desired);
		}
	}

	void NPCCamera::RestoreFOV()
	{
		if (!fovOverridden) return;
		if (auto* pc = RE::PlayerCamera::GetSingleton()) {
			pc->GetRuntimeData2().worldFOV = savedWorldFOV;
			SKSE::log::info("RestoreFOV: worldFOV -> {}", savedWorldFOV);
		}
		fovOverridden = false;
	}

	RE::Actor* NPCCamera::FindNearestNPC(RE::Actor* a_player, float a_radius)
	{
		// Fallback auto-target: when the crosshair has no NPC, walk every
		// actor the ProcessLists knows about, filter to living NPCs other
		// than the player, and pick the closest one within a_radius.
		if (!a_player) return nullptr;
		if (a_radius <= 0.0f) return nullptr;

		auto* pl = RE::ProcessLists::GetSingleton();
		if (!pl) {
			SKSE::log::warn("FindNearestNPC: ProcessLists singleton is null");
			return nullptr;
		}

		const RE::NiPoint3 playerPos = a_player->GetPosition();
		const std::uint32_t playerFormID = a_player->GetFormID();
		const float radiusSq = a_radius * a_radius;

		struct Best
		{
			RE::Actor* actor{ nullptr };
			float      distSq{ (std::numeric_limits<float>::max)() };
		} best;

		pl->ForAllActors([&](RE::Actor* a_actor) -> RE::BSContainer::ForEachResult {
			if (!a_actor) return RE::BSContainer::ForEachResult::kContinue;
			if (a_actor->GetFormID() == playerFormID) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			auto* base = a_actor->GetBaseObject();
			if (!base || base->GetFormType() != RE::FormType::NPC) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (a_actor->IsDead(true)) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (!a_actor->GetCurrent3D()) {
				return RE::BSContainer::ForEachResult::kContinue;
			}

			const RE::NiPoint3 d = playerPos - a_actor->GetPosition();
			const float distSq = d.SqrLength();
			if (distSq > radiusSq) {
				return RE::BSContainer::ForEachResult::kContinue;
			}
			if (distSq < best.distSq) {
				best.distSq = distSq;
				best.actor = a_actor;
			}
			return RE::BSContainer::ForEachResult::kContinue;
		});

		if (Settings::Debug.GetValue()) {
			if (best.actor) {
				SKSE::log::info("FindNearestNPC: picked formID=0x{:08X} distSq={:.2f}",
					best.actor->GetFormID(), best.distSq);
			}
			else {
				SKSE::log::info("FindNearestNPC: no NPC within radius {:.1f}", a_radius);
			}
		}
		return best.actor;
	}

	void NPCCamera::ApplyCameraTransform(RE::TESCamera* a_camera)
	{
		// Fast path: bail out cheaply when not attached. Attach/Detach are
		// rare, so we don't take the mutex until we know we need it.
		if (!attached) return;

		if (!a_camera) return;
		auto* root = a_camera->cameraRoot.get();
		if (!root) return;

		std::lock_guard<std::mutex> guard(lock);
		if (!attached) return;

		auto* actor = static_cast<RE::Actor*>(target.get());
		auto* head = GetHeadNode(actor);
		if (!head) {
			// 3D not loaded this frame: leave the engine's default transform
			// so the camera does not snap to origin. Warn once.
			static bool warned = false;
			if (!warned && Settings::Debug.GetValue()) {
				warned = true;
				SKSE::log::warn("ApplyCameraTransform: attached but head node is null (3D not loaded?). Logged once.");
			}
			return;
		}

		const RE::NiMatrix3 engineRotate = root->world.rotate;
		const RE::NiMatrix3 headRotate = head->world.rotate;

		// Extract engine heading/pitch from the engine-computed forward
		// vector. These angles are the pure consequence of player mouse
		// input plus the engine's own baseline; we only ever DIFFERENCE
		// them, never take them as absolute, so no composition-order bug.
		float engH = 0.0f;
		float engP = 0.0f;
		ExtractHeadingPitch(engineRotate, engH, engP);

		// Heading/pitch of the live NPC head node in world space. This is
		// the reference orientation both modes work from (mode 1 reads it
		// every frame, mode 2 reads it during the snap phase and freezes
		// a snapshot at handoff). Logged for diagnosing Mode-1 tracking.
		float headH = 0.0f;
		float headP = 0.0f;
		ExtractHeadingPitch(headRotate, headH, headP);

		// --- Debug-only throttled telemetry (1 line per ~180 frames).
		//     When bDebug=1 and the user suspects rotation tracking is
		//     wrong, this lets them verify:
		//       * mode value read from INI
		//       * head world H/P actually CHANGES when the NPC turns its
		//         head/body (proves live head tracking isn't stale)
		//       * engine H/P delta from baseline (shows mouse movement)
		static std::uint32_t s_dbgTick = 0;
		const bool dbgPrint = Settings::Debug.GetValue() &&
			(++s_dbgTick % 180 == 0);

		const auto mode = Settings::CameraRotationMode.GetValue();
		if (mode == 1u) {
			// ---- Mode 1: current head orientation + TOTAL mouse delta since Attach ----
			// Capture engine baseline lazily on the first frame so any
			// ForceThirdPerson camera-settle transient has already passed.
			if (!haveAttachBaseline) {
				attachEngineHeading = engH;
				attachEnginePitch   = engP;
				haveAttachBaseline  = true;
				if (Settings::Debug.GetValue()) {
					SKSE::log::info(
						"[Mode1] Attach baseline: engH={:.3f}deg engP={:.3f}deg  "
						"headH={:.3f}deg headP={:.3f}deg",
						attachEngineHeading * 57.2957795f,
						attachEnginePitch   * 57.2957795f,
						headH * 57.2957795f,
						headP * 57.2957795f);
				}
			}
			// Total delta (engine current - engine at Attach), both signs
			// negated per user-facing gesture mapping:
			//   yaw:   drag left  -> engH decreases -> -dH > 0 -> turn LEFT ✓
			//   pitch: push fwd   -> engP decreases -> -dP > 0 -> look UP ✓
			const float dH = -AngleDiff(engH, attachEngineHeading);
			const float dP = -AngleDiff(engP, attachEnginePitch);

			if (dbgPrint) {
				SKSE::log::info(
					"[Mode1] LIVE head H/P = ({:.3f}, {:.3f})deg | "
					"mouse delta (sign-corrected) dH/dP = ({:.3f}, {:.3f})deg | "
					"baseline eng H/P = ({:.3f}, {:.3f})deg | current eng H/P = ({:.3f}, {:.3f})deg",
					headH * 57.2957795f, headP * 57.2957795f,
					dH    * 57.2957795f, dP    * 57.2957795f,
					attachEngineHeading * 57.2957795f, attachEnginePitch * 57.2957795f,
					engH * 57.2957795f, engP * 57.2957795f);
			}
			(void)dH; (void)dP;
		}
		else {
			// ---- Mode 2 (default): snap to head, then seamless handoff ----
			// snapFrames counts how long we've been stuck in the snap phase
			// without any real mouse movement. After ~10s we emit a one-shot
			// hint because the snap phase is visually identical to Mode 1
			// (both track the live head rotation) and users often think the
			// mode toggle didn't work.
			static std::uint32_t s_snapFrames = 0;
			static bool         s_snapHintSent = false;
			if (snapActive) {
				++s_snapFrames;
				if (!s_snapHintSent && s_snapFrames > 600 &&
					Settings::Debug.GetValue()) {
					s_snapHintSent = true;
					SKSE::log::info(
						"[Mode2 snap] >10s still in snap phase (tracking live"
						" head rotation -- identical to Mode 1 visually)."
						" MOVE THE MOUSE (>~2deg) to trigger handoff and see"
						" the Mode 1/Mode 2 difference.");
				}
				if (graceFrames > 0) {
					--graceFrames;
				}
				else if (haveLastEngineAngles) {
					// Detect real mouse movement via heading/pitch change.
					const float dH = AngleDiff(engH, lastEngineHeading);
					const float dP = AngleDiff(engP, lastEnginePitch);
					constexpr float kAngleEps = 2.0f * 3.14159265358979f / 180.0f;  // 2 deg
					if (std::fabs(dH) > kAngleEps || std::fabs(dP) > kAngleEps) {
						// Handoff moment (matches user-defined "starting
						// angle"): freeze both the engine angles AND the
						// current head world matrix. Later we use
						// (eng - handoffEng) with sign correction so the
						// final rotation at handoff instant will equal
						// handoffHeadRotate exactly -- 100% continuous.
						handoffEngineHeading = engH;
						handoffEnginePitch   = engP;
						handoffHeadRotate    = headRotate;
						snapActive  = false;
						handoffDone = true;

						float hh = 0.0f, hp = 0.0f;
						ExtractHeadingPitch(handoffHeadRotate, hh, hp);
						SKSE::log::info(
							"ApplyCameraTransform: mode-2 handoff to pure"
							" mouse control. head H/P at handoff = ({:.3f}, {:.3f})deg,"
							" eng H/P baseline = ({:.3f}, {:.3f})deg",
							hh * 57.2957795f, hp * 57.2957795f,
							handoffEngineHeading * 57.2957795f,
							handoffEnginePitch   * 57.2957795f);
					}
				}
				lastEngineHeading = engH;
				lastEnginePitch   = engP;
				haveLastEngineAngles = true;

				if (dbgPrint) {
					SKSE::log::info(
						"[Mode2 snap] LIVE head H/P = ({:.3f}, {:.3f})deg |"
						" eng H/P = ({:.3f}, {:.3f})deg | graceFrames={}",
						headH * 57.2957795f, headP * 57.2957795f,
						engH  * 57.2957795f, engP  * 57.2957795f,
						graceFrames);
				}
			}
			else {
				// We left snap phase. Reset snap-stage telemetry counters so
				// the next Attach (new Mode-2 session) can hint again.
				s_snapFrames = 0;
				s_snapHintSent = false;
				if (dbgPrint && handoffDone) {
					const float dH = -AngleDiff(engH, handoffEngineHeading);
					const float dP = -AngleDiff(engP, handoffEnginePitch);
					float hh = 0.0f, hp = 0.0f;
					ExtractHeadingPitch(handoffHeadRotate, hh, hp);
					SKSE::log::info(
						"[Mode2 post-handoff] FROZEN head@handoff H/P = ({:.3f}, {:.3f})deg |"
						" mouse delta dH/dP = ({:.3f}, {:.3f})deg |"
						" CURRENT live head H/P NOW  = ({:.3f}, {:.3f})deg <<-- compare to frozen to see tracking difference",
						hh * 57.2957795f, hp * 57.2957795f,
						dH * 57.2957795f, dP * 57.2957795f,
						headH * 57.2957795f, headP * 57.2957795f);
				}
			}
		}

		// Compute the final effective camera rotation.
		RE::NiMatrix3 finalRotate;
		if (mode == 1u) {
			// Mode 1: current head orientation + signed total mouse delta
			// since Attach. Formally:
			//   final = Rz(-(engH - attachEngH)) * headRotate_now
			//                         * Rx(-(engP - attachEngP))
			// Tracking the head live every frame is what makes "continue
			// tracking head angle after mouse move" work.
			const float dH = -AngleDiff(engH, attachEngineHeading);
			const float dP = -AngleDiff(engP, attachEnginePitch);
			finalRotate = ComposeYawPitchOffset(headRotate, dH, dP);
		}
		else if (snapActive) {
			// Mode 2 before handoff: pure head orientation (both rotate
			// and head-pos anchoring track the live head node each frame).
			finalRotate = headRotate;
		}
		else if (handoffDone) {
			// Mode 2 after handoff: "final angle = start angle + mouse
			// delta". Start angle is handoffHeadRotate (frozen at handoff).
			// Mouse delta uses both signs negated for the same left/up
			// gesture-mapping reasons.
			const float dH = -AngleDiff(engH, handoffEngineHeading);
			const float dP = -AngleDiff(engP, handoffEnginePitch);
			finalRotate = ComposeYawPitchOffset(handoffHeadRotate, dH, dP);
		}
		else {
			// Safety (shouldn't happen): fall back to engine rotation.
			finalRotate = engineRotate;
		}

		// Anchor the camera root to the head position and apply the final
		// rotation (which already includes head-facing + any mouse offsets
		// or handoff deltas).
		RE::NiTransform desired = root->world;
		desired.translate = head->world.translate;
		desired.rotate = finalRotate;

		// View-space offset, rotated by the camera's effective world
		// rotation (headRotate * mouseOffset in mode 1; headRotate during
		// mode-2 snap; engineRotate * handoffOffset after mode-2 handoff,
		// etc.) so +Y stays "forward" relative to where the view faces.
		RE::NiPoint3 offset{
			Settings::CameraOffsetX.GetValue(),
			Settings::CameraOffsetY.GetValue(),
			Settings::CameraOffsetZ.GetValue()
		};
		RE::NiPoint3 rotated = desired.rotate * offset;
		desired.translate += rotated;

		// Write back through local so the parent chain stays consistent, then
		// propagate to world (and children) via NiAVObject::Update. This is
		// the same pattern Cinematic-Mode uses to relocate the camera root.
		if (root->parent) {
			root->local = root->parent->world.Invert() * desired;
		}
		else {
			root->local = desired;
		}

		RE::NiUpdateData updateData{};
		root->Update(updateData);

		static bool logged = false;
		if (!logged && Settings::Debug.GetValue()) {
			logged = true;
			SKSE::log::info("ApplyCameraTransform: anchored to head world=({:.2f}, {:.2f}, {:.2f})",
				head->world.translate.x, head->world.translate.y, head->world.translate.z);
		}
	}

	void NPCCamera::CameraUpdateThunk(RE::TESCamera* a_camera)
	{
		// Let the original update run first so the engine sets the rotation
		// from mouse look and computes the default camera position. Then, if
		// attached, we override the cameraRoot NiNode's world translate to
		// match the target NPC's head node.
		g_origCameraUpdate(a_camera);
		Get().ApplyCameraTransform(a_camera);
	}

	void NPCCamera::Install()
	{
		// Cinematic-Mode uses 14 bytes of trampoline for one 5-byte write_call
		// plus its safety margin; mirror that to avoid running out of space.
		SKSE::AllocTrampoline(14);
		auto& trampoline = SKSE::GetTrampoline();

		// PlayerCamera::Update (SE: 49852, AE: 50784). Inside the function,
		// at offset 0x1A6, there is a call to a sub-function taking a
		// TESCamera* (likely the inner TESCamera::Update). We patch that call
		// so our thunk runs instead; the thunk invokes the original and then
		// overrides the cameraRoot NiNode transform to anchor the camera to
		// the NPC head.
		const REL::Relocation playerCameraUpdate{ REL::RelocationID(49852, 50784) };
		const std::uintptr_t callSite =
			playerCameraUpdate.address() + REL::Relocate(0x1A6, 0x1A6, 0x1A6);
		g_origCameraUpdate = trampoline.write_call<5>(
			callSite,
			&NPCCamera::CameraUpdateThunk);

		SKSE::log::info("Install: PlayerCamera::Update call-site hook at 0x{:X}+0x1A6=0x{:X}, orig=0x{:X}",
			static_cast<unsigned long long>(playerCameraUpdate.address()),
			static_cast<unsigned long long>(callSite),
			static_cast<unsigned long long>(g_origCameraUpdate.address()));
	}
}
