#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// A global keyboard shortcut for RegisterHotKey
	struct Hotkey
	{
		unsigned int modifiers = 0; // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN
		unsigned int key = 0;       // virtual key: 'A'-'Z', '0'-'9', VK_F1-VK_F24, VK_SPACE
	};

	// "Ctrl+Alt+Space", "Win+Shift+A", "Ctrl+F12"; case ignored, Spanish names too ("Mayús",
	// "Espacio"). nullopt for an unknown part, or without Ctrl, Alt or Win: a lone key (or Shift+key)
	// would be taken from every program.
	std::optional<Hotkey> ParseHotkey(std::wstring_view text);

	// How the menus show it: "Ctrl+Alt+Espacio", or with English names "Ctrl+Alt+Space"
	std::wstring FormatHotkey(const Hotkey& hotkey, bool spanishNames = true);
}
