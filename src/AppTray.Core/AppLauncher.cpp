#include "AppLauncher.h"

#include "StringUtil.h"

#include <windows.h>

#include <shellapi.h>

#include <format>

#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		LaunchResult Execute(const wchar_t* verb, const fs::path& file, const std::wstring& parameters, const fs::path& directory)
		{
			SHELLEXECUTEINFOW info{};
			info.cbSize = sizeof(info);
			info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
			info.lpVerb = verb;
			info.lpFile = file.c_str();
			info.lpParameters = parameters.empty() ? nullptr : parameters.c_str();
			info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
			info.nShow = SW_SHOWNORMAL;

			LaunchResult result;
			if (ShellExecuteExW(&info))
			{
				result.started = true;
				return result;
			}
			result.error = GetLastError();
			result.cancelled = result.error == ERROR_CANCELLED;
			return result;
		}
	}

	LaunchResult LaunchProgram(const fs::path& target, std::wstring_view arguments, bool asAdmin, const fs::path& directory)
	{
		return Execute(
			asAdmin ? L"runas" : nullptr, target, std::wstring(arguments), directory.empty() ? target.parent_path() : directory);
	}

	LaunchResult OpenWithShell(const fs::path& path)
	{
		return Execute(nullptr, path, {}, {});
	}

	LaunchResult RevealInExplorer(const fs::path& file)
	{
		return Execute(nullptr, L"explorer.exe", std::format(L"/select,\"{}\"", file.wstring()), {});
	}

	std::wstring DescribeLaunchError(const LaunchResult& result)
	{
		wchar_t* buffer = nullptr;
		const DWORD length = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			result.error,
			0,
			reinterpret_cast<wchar_t*>(&buffer),
			0,
			nullptr);
		std::wstring text = length > 0 && buffer != nullptr ? Trim(std::wstring(buffer, length)) : std::wstring{};
		LocalFree(buffer);
		return text.empty() ? std::format(L"error {}", result.error) : text;
	}
}
