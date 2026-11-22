#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// A small modal window: a label, one line of text and OK / Cancel (Enter / Esc). Centered over
	// the owner, which stays disabled meanwhile. nullopt when cancelled.
	std::optional<std::wstring> ShowTextPrompt(
		HINSTANCE instance, HWND owner, std::wstring_view title, std::wstring_view label, std::wstring_view initialText);
}
