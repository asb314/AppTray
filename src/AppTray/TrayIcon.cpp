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

	bool TrayIcon::Add(HWND owner, UINT callbackMessage, HICON icon, std::wstring_view tip, const std::optional<GUID>& guid)
	{
		data_ = {};
		added_ = false; // also when called again after "TaskbarCreated": the old icon is gone
		data_.cbSize = sizeof(data_);
		data_.hWnd = owner;
		data_.uID = TrayIconId;
		data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
		data_.uCallbackMessage = callbackMessage;
		data_.hIcon = icon;
		CopyText(data_.szTip, tip);
		if (guid)
		{
			// A GUID gives the icon a stable identity: Windows remembers whether the user keeps it
			// on the taskbar, and an icon left behind by a crashed copy is replaced, not doubled
			data_.uFlags |= NIF_GUID;
			data_.guidItem = *guid;
			Shell_NotifyIconW(NIM_DELETE, &data_); // a ghost from a copy that did not exit cleanly
			added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
			if (!added_)
			{
				// Windows binds the GUID to the exe's path: after moving the folder it refuses it
				data_.uFlags &= ~NIF_GUID;
				data_.guidItem = {};
			}
		}
		if (!added_)
		{
			added_ = Shell_NotifyIconW(NIM_ADD, &data_) != FALSE;
		}
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
			const UINT flags = data_.uFlags;
			data_.uFlags = NIF_TIP | NIF_SHOWTIP | (flags & NIF_GUID);
			Shell_NotifyIconW(NIM_MODIFY, &data_);
			data_.uFlags = flags;
		}
	}

	void TrayIcon::ShowNotification(std::wstring_view title, std::wstring_view text, bool warning)
	{
		if (!added_)
		{
			return;
		}
		NOTIFYICONDATAW info = data_;
		info.uFlags = NIF_INFO | (data_.uFlags & NIF_GUID);
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
		if ((data_.uFlags & NIF_GUID) != 0)
		{
			identifier.guidItem = data_.guidItem;
		}
		RECT rect{};
		if (FAILED(Shell_NotifyIconGetRect(&identifier, &rect)))
		{
			return std::nullopt;
		}
		return rect;
	}
}
