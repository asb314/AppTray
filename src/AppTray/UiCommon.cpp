#include "UiCommon.h"

#include "StringUtil.h"

#include <shellscalingapi.h>

#include <cstdint>
#include <format>
#include <functional>

#pragma comment(lib, "shcore.lib")

namespace apptray
{
	std::wstring GetInstanceName(const std::wstring& exeDirectory)
	{
		// The folder's identity (volume serial and file id), not its spelling: a mapped drive and
		// its UNC path, a subst drive or an 8.3 name are the same AppTray
		wil::unique_hfile folder(CreateFileW(
			exeDirectory.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
		BY_HANDLE_FILE_INFORMATION info{};
		if (folder && GetFileInformationByHandle(folder.get(), &info))
		{
			return std::format(
				L"AppTray-{:08x}-{:08x}{:08x}", info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow);
		}
		return std::format(L"AppTray-{:x}", std::hash<std::wstring>{}(ToLower(exeDirectory)));
	}

	GUID GetTrayIconGuid(const std::wstring& instanceName)
	{
		GUID guid{0x6f1c2e3a, 0x8b4d, 0x4c55, {}};
		const std::uint64_t hash = std::hash<std::wstring>{}(instanceName);
		for (int i = 0; i < 8; ++i)
		{
			guid.Data4[i] = static_cast<unsigned char>(hash >> (8 * i));
		}
		guid.Data4[0] = static_cast<unsigned char>((guid.Data4[0] & 0x3F) | 0x80); // RFC 4122 variant
		return guid;
	}

	wil::unique_hfont CreateMessageFont(UINT dpi, int percent)
	{
		NONCLIENTMETRICSW metrics{};
		metrics.cbSize = sizeof(metrics);
		SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi);
		LOGFONTW font = metrics.lfMessageFont;
		font.lfHeight = MulDiv(font.lfHeight, percent, 100);
		return wil::unique_hfont(CreateFontIndirectW(&font));
	}

	UINT GetDpiForRect(const RECT& rect)
	{
		HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
		UINT dpiX = USER_DEFAULT_SCREEN_DPI;
		UINT dpiY = USER_DEFAULT_SCREEN_DPI;
		if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
		{
			return USER_DEFAULT_SCREEN_DPI;
		}
		return dpiX;
	}

	std::wstring GetWindowTextString(HWND window)
	{
		const int length = GetWindowTextLengthW(window);
		std::wstring text(static_cast<size_t>(length) + 1, L'\0');
		GetWindowTextW(window, text.data(), length + 1);
		text.resize(static_cast<size_t>(length));
		return text;
	}

	bool IsKeyDown(int virtualKey)
	{
		return (GetKeyState(virtualKey) & 0x8000) != 0;
	}
}
