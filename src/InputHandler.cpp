#include "InputHandler.h"

#include "NPCCamera.h"
#include "Settings.h"

#include "RE/Skyrim.h"  // BSInputDeviceManager, ButtonEvent, CrosshairPickData, Actor, PlayerCharacter, ...
#include "SKSE/SKSE.h"  // SKSE::log for debug logging

#include <windows.h>  // MapVirtualKeyW, MAPVK_VK_TO_VSC

namespace NPCIC
{
	void InputHandler::Register()
	{
		if (registered) return;

		// ButtonEvent keyboard idCode is the DirectInput DIK scan code. Convert
		// the user-facing Win32 virtual-key code once at registration.
		hotkeyCode = static_cast<std::uint32_t>(
			::MapVirtualKeyW(Settings::ToggleHotKey.GetValue(), MAPVK_VK_TO_VSC));
		SKSE::log::info("InputHandler: hotkey VK=0x{:X}, DIK scan=0x{:X}",
			static_cast<unsigned int>(Settings::ToggleHotKey.GetValue()),
			static_cast<unsigned int>(hotkeyCode));

		// BSInputDeviceManager is a runtime singleton the game constructs during
		// input-system init; it is NOT available at SKSEPluginLoad time. This is
		// called from the kInputLoaded message (with kPostLoad/kPostPostLoad
		// retries), so it should be valid here. If still null, bail and let the
		// next message retry.
		auto* mgr = RE::BSInputDeviceManager::GetSingleton();
		if (!mgr) {
			SKSE::log::warn("InputHandler: BSInputDeviceManager still null, deferring to next message");
			return;
		}
		mgr->AddEventSink(this);
		registered = true;
		SKSE::log::info("InputHandler: AddEventSink OK");
	}

	RE::BSEventNotifyControl InputHandler::ProcessEvent(
		RE::InputEvent* const* a_event,
		RE::BSTEventSource<RE::InputEvent*>* /*a_source*/)
	{
		if (!a_event) return RE::BSEventNotifyControl::kContinue;

		// Heartbeat: proves the sink is alive and events are flowing. The sink
		// is polled every frame, so throttle to one line every 300 calls.
		static std::uint32_t heartbeat = 0;
		if ((++heartbeat % 300) == 0 && Settings::Debug.GetValue()) {
			SKSE::log::info("ProcessEvent heartbeat #{}", heartbeat);
		}

		for (const RE::InputEvent* ev = *a_event; ev; ev = ev->next) {
			if (ev->GetEventType() != RE::INPUT_EVENT_TYPE::kButton) continue;
			const auto* btn = ev->AsButtonEvent();
			if (!btn) continue;
			if (btn->GetDevice() != RE::INPUT_DEVICE::kKeyboard) continue;
			if (!btn->IsDown()) continue;  // fire only on the press frame

			// A key was pressed this frame. Low-frequency, safe to log.
			if (Settings::Debug.GetValue()) {
				SKSE::log::info("Key down: DIK idCode=0x{:X}, expected=0x{:X}, match={}",
					static_cast<unsigned int>(btn->GetIDCode()),
					static_cast<unsigned int>(hotkeyCode),
					btn->GetIDCode() == hotkeyCode);
			}

			if (btn->GetIDCode() != hotkeyCode) continue;

			auto& cam = NPCCamera::Get();
			if (cam.IsAttached()) {
				SKSE::log::info("Hotkey -> Detach (was attached)");
				cam.Detach();
				continue;
			}

			// Acquire the crosshair target as the NPC to view.
			RE::Actor* targetActor = nullptr;
			if (auto* pick = RE::CrosshairPickData::GetSingleton()) {
				RE::NiPointer<RE::TESObjectREFR> ref = pick->GetActiveTarget().get();
				if (ref) {
					auto* base = ref->GetBaseObject();
					// NPC references are always Character / PlayerCharacter
					// instances, both of which are Actor subclasses, so once the
					// base form is a TESNPC we may treat the reference as an
					// Actor. dynamic_cast<Actor*> is avoided because the game's
					// RTTI names are unnamespaced ("Character") while
					// CommonLibSSE wraps them in namespace RE ("RE::Character");
					// MSVC's dynamic_cast compares decorated names and the
					// mismatch makes the cast return null on a valid NPC. The
					// memory layout matches (offsets are reverse-engineered),
					// so static_cast is safe here.
					if (base && base->GetFormType() == RE::FormType::NPC) {
						targetActor = static_cast<RE::Actor*>(ref.get());
					}
					if (Settings::Debug.GetValue()) {
						SKSE::log::info("Crosshair target formID=0x{:08X}, isNPC={}, rtti={}, baseFormType={}",
							ref->GetFormID(),
							targetActor != nullptr,
							typeid(*ref.get()).name(),
							base ? static_cast<unsigned int>(base->GetFormType()) : 0xFFFFFFFFu);
					}
				}
				else if (Settings::Debug.GetValue()) {
					SKSE::log::info("Crosshair active target is null (aim at an NPC)");
				}
			}
			else {
				SKSE::log::warn("CrosshairPickData singleton is null");
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (targetActor && targetActor != player) {
				SKSE::log::info("Attaching to NPC formID=0x{:08X}",
					targetActor->GetFormID());
				cam.Attach(targetActor);
			}
			else {
				// No crosshair NPC: try the auto-target fallback if enabled.
				const float radius = Settings::AutoTargetRadius.GetValue();
				if (radius > 0.0f && player) {
					SKSE::log::info("No crosshair NPC; searching radius {:.1f}",
						radius);
					auto* found = NPCCamera::FindNearestNPC(
						static_cast<RE::Actor*>(player), radius);
					if (found) {
						SKSE::log::info("Attaching to auto-target NPC formID=0x{:08X}",
							found->GetFormID());
						cam.Attach(found);
					}
					else if (Settings::Debug.GetValue()) {
						SKSE::log::info("Not attaching (no NPC in radius)");
					}
				}
				else if (Settings::Debug.GetValue()) {
					SKSE::log::info("Not attaching (no valid NPC at crosshair)");
				}
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}
}
