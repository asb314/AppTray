#include "TrayIcon.h"

#include <wchar.h>

#include <algorithm>

namespace apptray
{
	namespace
	{
		constexpr UINT TrayIconId = 1;

		template <size_t N> void CopyText(wchar_t (&target)[N], std::wstring_view text)
		{
			const size_t length = std::min(text.size(), N - 1);
			wmemcpy(target, text.data(), length);
			target[length] = L'\0';
		}
	}

	TrayIcon::~TrayIcon()
	{
		Remove();
	}

	bool TrayIcon::Add(HWND owner, UINT callbackMessage, HICON icon, std::wstring_view tip)
	{
		data_ = {};
		data_.cbSize = sizeof(data_);
		data_.hWnd = owner;
		data_.uID = TrayIconId;
		data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
		data_.uCallbackMessage = callbackMessage;
		data_.hIcon = icon;
		CopyText(data_.szTip, tip);
		added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
		if (added_)
		{
			data_.uVersion = NOTIFYICON_VERSION_4;
			Shell_NotifyIconW(NIM_SETVERSION, &data_);
		}
		return added_;
	}

	void TrayIcon::Remove()
	{
		if (added_)
		{
			Shell_NotifyIconW(NIM_DELETE, &data_);
			added_ = false;
		}
	}

	void TrayIcon::SetTip(std::wstring_view tip)
	{
		CopyText(data_.szTip, tip);
		if (added_)
		{
			data_.uFlags = NIF_TIP | NIF_SHOWTIP;
			Shell_NotifyIconW(NIM_MODIFY, &data_);
			data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
		}
	}

	void TrayIcon::ShowNotification(std::wstring_view title, std::wstring_view text, bool warning)
	{
		if (!added_)
		{
			return;
		}
		NOTIFYICONDATAW info = data_;
		info.uFlags = NIF_INFO;
		info.dwInfoFlags = (warning ? NIIF_WARNING : NIIF_INFO) | NIIF_RESPECT_QUIET_TIME;
		CopyText(info.szInfoTitle, title);
		CopyText(info.szInfo, text);
		Shell_NotifyIconW(NIM_MODIFY, &info);
	}

	std::optional<RECT> TrayIcon::GetRect() const
	{
		if (!added_)
		{
			return std::nullopt;
		}
		NOTIFYICONIDENTIFIER identifier{};
		identifier.cbSize = sizeof(identifier);
		identifier.hWnd = data_.hWnd;
		identifier.uID = data_.uID;
		RECT rect{};
		if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect)))
		{
			return std::nullopt;
		}
		return rect;
	}
}
