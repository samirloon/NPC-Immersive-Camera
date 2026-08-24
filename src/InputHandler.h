#pragma once

#include "RE/B/BSTEvent.h"
#include "RE/I/InputEvent.h"

namespace RE
{
	class InputEvent;
}

namespace NPCIC
{
	// Listens for keyboard input on the game's input event source. Detects the
	// configured hotkey and toggles NPC view on/off, acquiring the current
	// crosshair target as the view subject.
	class InputHandler : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler& Get() { static InputHandler instance; return instance; }

		void Register();

		RE::BSEventNotifyControl ProcessEvent(
			RE::InputEvent* const* a_event,
			RE::BSTEventSource<RE::InputEvent*>* a_source) override;

	private:
		InputHandler() = default;

		// DIK scan code derived from the configured Win32 virtual-key code.
		std::uint32_t hotkeyCode{ 0 };
		bool            registered{ false };  // guards against double AddEventSink
	};
}
