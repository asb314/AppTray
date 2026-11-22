#include "AppWindows.h"

#include <algorithm>

namespace apptray
{
	namespace
	{
		struct WindowSearch
		{
			const std::vector<unsigned long>* processIds = nullptr;
			std::vector<HWND> windows;
		};

		BOOL CALLBACK CollectWindow(HWND window, LPARAM data)
		{
			WindowSearch& search = *reinterpret_cast<WindowSearch*>(data);
			DWORD processId = 0;
			GetWindowThreadProcessId(window, &processId);
			if (std::find(search.processIds->begin(), search.processIds->end(), processId) == search.processIds->end())
			{
				return TRUE;
			}
			if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr
				|| (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 || GetWindowTextLengthW(window) == 0)
			{
				return TRUE;
			}
			search.windows.push_back(window);
			return TRUE;
		}
	}

	std::vector<HWND> FindMainWindows(const std::vector<unsigned long>& processIds)
	{
		WindowSearch search;
		search.processIds = &processIds;
		if (!processIds.empty())
		{
			EnumWindows(&CollectWindow, reinterpret_cast<LPARAM>(&search)); // top of the Z order first
		}
		return std::move(search.windows);
	}

	bool ActivateWindow(HWND window)
	{
		if (IsIconic(window))
		{
			ShowWindow(window, SW_RESTORE);
		}
		return SetForegroundWindow(window) != FALSE;
	}

	bool RequestWindowClose(HWND window)
	{
		// User Interface Privilege Isolation drops it (ERROR_ACCESS_DENIED) for an elevated window
		return PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
	}
}
