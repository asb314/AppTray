#include "ProcessFinder.h"

#include "StringUtil.h"

#include <windows.h>

#include <tlhelp32.h>

#include <wil/resource.h>

#include <algorithm>

namespace apptray
{
	std::vector<std::wstring> GetRunningImagePaths()
	{
		std::vector<std::wstring> paths;
		wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE)
		{
			return paths;
		}
		PROCESSENTRY32W entry{};
		entry.dwSize = sizeof(entry);
		for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry))
		{
			wil::unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
			if (!process)
			{
				continue;
			}
			wchar_t buffer[MAX_PATH * 2]{};
			DWORD length = static_cast<DWORD>(std::size(buffer));
			if (QueryFullProcessImageNameW(process.get(), 0, buffer, &length))
			{
				paths.push_back(ToLower(std::wstring_view(buffer, length)));
			}
		}
		return paths;
	}

	bool IsAppRunning(const std::vector<std::wstring>& runningImages, const std::filesystem::path& path, bool isFolder)
	{
		std::wstring target = ToLower(path.lexically_normal().wstring());
		if (isFolder)
		{
			while (!target.empty() && target.back() == L'\\')
			{
				target.pop_back();
			}
			target += L'\\';
			return std::any_of(
				runningImages.begin(), runningImages.end(), [&](const std::wstring& image) { return image.starts_with(target); });
		}
		return std::find(runningImages.begin(), runningImages.end(), target) != runningImages.end();
	}
}
