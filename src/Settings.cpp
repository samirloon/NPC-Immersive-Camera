#include "Settings.h"

#include "REX/REX/INI.h"

#include <windows.h>  // GetModuleHandleW, GetModuleFileNameW, HMODULE, wchar_t, MAX_PATH

#include <filesystem>
#include <string>

namespace NPCIC::Settings
{
	void Load()
	{
		// -----------------------------------------------------------------
		// Resolve the absolute path to NPCIC.ini, which MUST live next to
		// NPCIC.dll in Data/SKSE/Plugins (standard SKSE plugin INI layout).
		//
		// We CANNOT rely on the process current working directory: the
		// game CWD is the Skyrim root (where TESV.exe / SkyrimSE.exe lives),
		// and the INI file is not there. REX::SettingStore::Init() only
		// stores std::string_views (no ownership), so keep the resolved
		// absolute path string in a process-lifetime static for the views
		// to stay valid after Init() returns, then call Load().
		// -----------------------------------------------------------------
		static std::string s_resolvedIniPath;

		if (s_resolvedIniPath.empty()) {
			std::filesystem::path ini;

			wchar_t dllPathBuf[MAX_PATH]{};
			if (HMODULE dll = ::GetModuleHandleW(L"NPCIC.dll")) {
				const DWORD len = ::GetModuleFileNameW(dll, dllPathBuf, MAX_PATH);
				if (len > 0 && len < MAX_PATH) {
					// DLL path like "...\Data\SKSE\Plugins\NPCIC.dll"
					ini = std::filesystem::path(dllPathBuf).parent_path() / L"NPCIC.ini";
				}
			}

			// Fallback (rare): module lookup failed because the DLL name
			// changed at deploy-time. Guess relative to CWD which is
			// almost always the Skyrim root directory.
			if (ini.empty()) {
				ini = std::filesystem::path(L"Data") / L"SKSE" / L"Plugins" / L"NPCIC.ini";
			}

			// path::string() under MSVC yields a narrow (CP_ACP) string.
			// The plugin name + "Data/SKSE/Plugins" are always ASCII, so
			// CP_ACP == UTF-8 superset for this specific case. CSimpleIniA
			// (used internally by REX::INI) takes narrow file paths via
			// LoadFile(const char*).
			s_resolvedIniPath = ini.string();
		}

		auto* store = REX::INI::SettingStore::GetSingleton();
		store->Init(s_resolvedIniPath.c_str(), s_resolvedIniPath.c_str());
		store->Load();
	}
}
