#include "InputHandler.h"
#include "NPCCamera.h"
#include "Settings.h"

#include "SKSE/SKSE.h"

#include <windows.h>     // GetModuleHandleW, GetModuleFileNameW, HMODULE, MAX_PATH
#include <filesystem>  // path, exists

namespace
{
	// Restore NPC head scales (detach) when a save is loaded or a new game
	// starts, so a previous attach can never leave an NPC with a hidden head.
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) return;
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostLoad:
		case SKSE::MessagingInterface::kPostPostLoad:
		case SKSE::MessagingInterface::kInputLoaded:
			// BSInputDeviceManager is not constructed until the input system
			// initializes, which happens AFTER SKSEPluginLoad. Register the
			// input sink here instead. Register() is idempotent, so calling
			// it on each of these messages is safe; it succeeds on the first
			// one where the singleton is ready.
			NPCIC::InputHandler::Get().Register();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			NPCIC::NPCCamera::Get().Detach();
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_intfc)
{
	SKSE::Init(a_intfc);  // also initializes the logger (a_log defaults to true)

	SKSE::log::info("NPCIC v1-0-0-0");
	SKSE::log::info("NPC IC loading...");

	NPCIC::Settings::Load();

	// ----- Full settings dump right after Settings::Load() --------------------
	// This lets us spot instantly which INI values actually loaded vs which
	// silently fell back to the C++ hardcoded default (a common REX::INI
	// symptom when a key/section name does not match or the parser dislikes
	// the value format). NOTE: all keys are in [General] now because in the
	// user's earlier deploy REX::INI lost the entire second [Camera] section.
	SKSE::log::info("===== INI Settings dump (all keys live in [General] section) =====");
	{
		// Echo the exact resolved INI path so the user can confirm the file
		// they edited is the one the plugin actually opened. This has to run
		// AFTER Settings::Load() so the static resolved path inside
		// Settings.cpp is populated, but we stored our own copy there. To
		// avoid duplication, recompute here for logging purposes.
		wchar_t dllPathBuf[MAX_PATH]{};
		std::filesystem::path loggedIni;
		if (HMODULE dll = ::GetModuleHandleW(L"NPCIC.dll")) {
			const DWORD len = ::GetModuleFileNameW(dll, dllPathBuf, MAX_PATH);
			if (len > 0 && len < MAX_PATH) {
				loggedIni = std::filesystem::path(dllPathBuf).parent_path() / L"NPCIC.ini";
			}
		}
		if (loggedIni.empty()) {
			loggedIni = std::filesystem::path(L"Data") / L"SKSE" / L"Plugins" / L"NPCIC.ini";
		}
		const bool exists = std::filesystem::exists(loggedIni);
		SKSE::log::info("  INI resolved path  = {}  (exists={})",
			loggedIni.string(), exists ? "YES" : "NO -- file missing or wrong directory");
	}
	SKSE::log::info("  [General] iToggleHotKey       = 0x{:X}  (default 0x75)",
		static_cast<unsigned int>(NPCIC::Settings::ToggleHotKey.GetValue()));
	SKSE::log::info("  [General] bHideNpcHead        = {}   (default 1)",
		NPCIC::Settings::HideNpcHead.GetValue() ? 1 : 0);
	SKSE::log::info("  [General] bDebug              = {}   (default 1)",
		NPCIC::Settings::Debug.GetValue() ? 1 : 0);
	SKSE::log::info("  [General] fAutoTargetRadius   = {:.3f}  (default 50.0)",
		NPCIC::Settings::AutoTargetRadius.GetValue());
	SKSE::log::info("  [General] fCameraOffsetX      = {:.3f}  (default 0.0)",
		NPCIC::Settings::CameraOffsetX.GetValue());
	SKSE::log::info("  [General] fCameraOffsetY      = {:.3f}  (default 0.0)",
		NPCIC::Settings::CameraOffsetY.GetValue());
	SKSE::log::info("  [General] fCameraOffsetZ      = {:.3f}  (default 0.0)",
		NPCIC::Settings::CameraOffsetZ.GetValue());
	SKSE::log::info("  [General] fNpcViewFOV         = {:.3f}  (default 0.0)",
		NPCIC::Settings::NpcViewFOV.GetValue());
	SKSE::log::info("  [General] iCameraRotationMode = {}   (default 2; 1=always-track-head+delta, 2=snap+handoff)",
		static_cast<unsigned int>(NPCIC::Settings::CameraRotationMode.GetValue()));
	SKSE::log::info("===== end INI dump =====");

	NPCIC::NPCCamera::Install();        // ThirdPersonState::Update vtable hook

	// Input sink registration is deferred to OnMessage (kPostLoad/kPostPostLoad/
	// kInputLoaded) because BSInputDeviceManager is not yet constructed here.
	if (auto* msg = SKSE::GetMessagingInterface()) {
		msg->RegisterListener(&OnMessage);
	}

	SKSE::log::info("NPC IC loaded. Hotkey VK=0x{:X}, hideHead={}.",
		static_cast<unsigned int>(NPCIC::Settings::ToggleHotKey.GetValue()),
		NPCIC::Settings::HideNpcHead.GetValue());
	return true;
}
