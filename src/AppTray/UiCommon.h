#pragma once

#include <windows.h>

#include <wil/resource.h>

#include <string>

namespace apptray
{
	// Window messages between AppTray's windows, threads and instances
	inline constexpr UINT WM_APP_TRAY = WM_APP + 1;      // tray icon callback
	inline constexpr UINT WM_APP_SCAN_DONE = WM_APP + 2; // lParam: RefreshOutcome* (receiver owns it)
	inline constexpr UINT WM_APP_SHOW = WM_APP + 3;      // another instance: open the launcher
	inline constexpr UINT WM_APP_EXIT = WM_APP + 4;      // another instance: quit
	inline constexpr UINT WM_APP_ICON = WM_APP + 5;      // icon worker: lParam IconResult* (receiver owns it)
	inline constexpr UINT WM_APP_UPDATE_DONE = WM_APP + 6; // wParam: exit code of --check-update
	inline constexpr UINT WM_APP_TASK_DONE = WM_APP + 7;   // wParam: exit code of --register/--unregister-task
	inline constexpr UINT WM_APP_SHORTCUT_DONE = WM_APP + 8; // wParam: exit code of --create-shortcut
	inline constexpr UINT WM_APP_ABOUT = WM_APP + 9;         // show "Acerca de" once the menu is gone

	inline constexpr wchar_t TrayWindowClass[] = L"AppTray.TrayWindow";
	inline constexpr wchar_t LauncherWindowClass[] = L"AppTray.Launcher";

	// "AppTray-1a2b3c...": one per AppTray folder. Names the single-instance mutex and titles the
	// hidden tray window, so a second start talks to the copy of the same folder only.
	std::wstring GetInstanceName(const std::wstring& exeDirectory);

	// Tray icon GUID for an instance: fixed prefix, last bytes from the instance name, so each
	// AppTray folder has its own stable icon identity
	GUID GetTrayIconGuid(const std::wstring& instanceName);

	inline int ScaleForDpi(int value, UINT dpi)
	{
		return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
	}

	// The system message font at a given DPI, optionally scaled (percent)
	wil::unique_hfont CreateMessageFont(UINT dpi, int percent = 100);

	// DPI of the monitor that holds a rectangle
	UINT GetDpiForRect(const RECT& rect);

	std::wstring GetWindowTextString(HWND window);

	bool IsKeyDown(int virtualKey);
}
