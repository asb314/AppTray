#pragma once

#include <windows.h>

#include <shellapi.h>

#include <optional>
#include <string_view>

namespace apptray
{
	// The notification-area icon (Shell_NotifyIcon, version 4 callbacks)
	class TrayIcon
	{
	public:
		~TrayIcon();

		// Adds the icon; call again after Explorer restarts ("TaskbarCreated")
		bool Add(HWND owner, UINT callbackMessage, HICON icon, std::wstring_view tip);
		void Remove();
		void SetTip(std::wstring_view tip);
		void ShowNotification(std::wstring_view title, std::wstring_view text, bool warning);

		// Screen rectangle of the icon, to place the launcher next to it
		std::optional<RECT> GetRect() const;

	private:
		NOTIFYICONDATAW data_{};
		bool added_ = false;
	};
}
