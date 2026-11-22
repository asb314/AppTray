#pragma once

#include <windows.h>

#include <vector>

namespace apptray
{
	// The windows of some processes a user would switch to, as the taskbar shows them: top-level,
	// visible (minimized counts), not owned by another window, not a tool window, with a title.
	// Front-most first, so the first one is the one used last.
	std::vector<HWND> FindMainWindows(const std::vector<unsigned long>& processIds);

	// Restores a minimized window and brings it to the front. Needs the foreground: the caller
	// must be the foreground process (the launcher or the tray menu, right after a click).
	bool ActivateWindow(HWND window);

	// Asks a window to close, as its X button does (the program may still ask to save). False
	// when Windows blocks the request: the program runs as administrator and AppTray does not.
	bool RequestWindowClose(HWND window);
}
