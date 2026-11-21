#pragma once

#include <windows.h>

#include <wil/resource.h>

namespace apptray
{
	struct ThemePalette
	{
		COLORREF window;        // flyout background
		COLORREF surface;       // search box
		COLORREF text;
		COLORREF secondaryText; // categories, status line
		COLORREF border;
	};

	// Light or dark, following Windows' "app mode". Documented: the DWM attributes. Not
	// documented but widely used (Notepad++, Explorer++): uxtheme's SetPreferredAppMode for
	// dark context menus and the "DarkMode_Explorer" theme for list, tree and scroll bars.
	class Theme
	{
	public:
		static bool IsSystemDark();

		// Context menus of the whole process follow the given mode
		static void ApplyMenuMode(bool dark);

		Theme();

		void Reload();
		bool IsDark() const
		{
			return dark_;
		}
		const ThemePalette& Palette() const
		{
			return palette_;
		}
		HBRUSH WindowBrush() const
		{
			return windowBrush_.get();
		}
		HBRUSH SurfaceBrush() const
		{
			return surfaceBrush_.get();
		}

		// Rounded corners, border colour and dark frame for a top-level window
		void ApplyToWindow(HWND window) const;

		// Explorer theme (dark or light) for list views, tree views and scroll bars
		void ApplyToControl(HWND control) const;

	private:
		bool dark_ = false;
		ThemePalette palette_{};
		wil::unique_hbrush windowBrush_;
		wil::unique_hbrush surfaceBrush_;
	};
}
