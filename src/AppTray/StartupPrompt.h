#pragma once

#include <windows.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace apptray
{
	struct StartupChoice
	{
		size_t itemIndex = 0;
		std::wstring name;
		bool checked = false;
	};

	// The logon dialog: "Abrir también:" with a checkbox per app and a countdown. Touching a
	// checkbox stops the countdown. Returns the choices with their final checks when the user
	// accepts or the countdown ends; nullopt for "Ninguna" or closing it (nothing opens and the
	// saved choice stays as it was). Runs its own modal loop.
	std::optional<std::vector<StartupChoice>> ShowStartupPrompt(
		HINSTANCE instance, std::vector<StartupChoice> choices, int countdownSeconds);
}
