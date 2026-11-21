#include "Theme.h"

#include <dwmapi.h>
#include <uxtheme.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")

namespace apptray
{
	namespace
	{
		// uxtheme.dll exports without names (Windows 10 1903 and later)
		enum class PreferredAppMode
		{
			Default,
			AllowDark,
			ForceDark,
			ForceLight,
		};
		using SetPreferredAppModeFunction = PreferredAppMode(WINAPI*)(PreferredAppMode);
		using FlushMenuThemesFunction = void(WINAPI*)();
		constexpr WORD SetPreferredAppModeOrdinal = 135;
		constexpr WORD FlushMenuThemesOrdinal = 136;

		constexpr ThemePalette LightPalette{
			RGB(243, 243, 243), RGB(255, 255, 255), RGB(26, 26, 26), RGB(96, 96, 96), RGB(213, 213, 213)};
		constexpr ThemePalette DarkPalette{
			RGB(32, 32, 32), RGB(45, 45, 45), RGB(255, 255, 255), RGB(165, 165, 165), RGB(64, 64, 64)};
	}

	bool Theme::IsSystemDark()
	{
		// APPTRAY_THEME=light|dark forces a mode (screenshots and tests)
		wchar_t forced[8]{};
		if (GetEnvironmentVariableW(L"APPTRAY_THEME", forced, static_cast<DWORD>(std::size(forced))) > 0)
		{
			return _wcsicmp(forced, L"dark") == 0;
		}
		DWORD value = 1;
		DWORD size = sizeof(value);
		const LSTATUS status = RegGetValueW(
			HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme",
			RRF_RT_REG_DWORD,
			nullptr,
			&value,
			&size);
		return status == ERROR_SUCCESS && value == 0;
	}

	void Theme::ApplyMenuMode(bool dark)
	{
		HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
		if (uxtheme == nullptr)
		{
			uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
		}
		if (uxtheme == nullptr)
		{
			return;
		}
		const auto setMode = reinterpret_cast<SetPreferredAppModeFunction>(
			reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(SetPreferredAppModeOrdinal))));
		const auto flush = reinterpret_cast<FlushMenuThemesFunction>(
			reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(FlushMenuThemesOrdinal))));
		if (setMode != nullptr)
		{
			setMode(dark ? PreferredAppMode::ForceDark : PreferredAppMode::ForceLight);
		}
		if (flush != nullptr)
		{
			flush();
		}
	}

	Theme::Theme()
	{
		Reload();
	}

	void Theme::Reload()
	{
		dark_ = IsSystemDark();
		palette_ = dark_ ? DarkPalette : LightPalette;
		windowBrush_.reset(CreateSolidBrush(palette_.window));
		surfaceBrush_.reset(CreateSolidBrush(palette_.surface));
	}

	void Theme::ApplyToWindow(HWND window) const
	{
		const BOOL dark = dark_ ? TRUE : FALSE;
		DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
		const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
		DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
		const COLORREF border = palette_.border;
		DwmSetWindowAttribute(window, DWMWA_BORDER_COLOR, &border, sizeof(border));
	}

	void Theme::ApplyToControl(HWND control) const
	{
		SetWindowTheme(control, dark_ ? L"DarkMode_Explorer" : L"Explorer", nullptr);
	}
}
