#include "ProcessFinder.h"

#include "StringUtil.h"

#include <windows.h>

#include <tlhelp32.h>

#include <wil/resource.h>

namespace apptray
{
	std::vector<RunningProcess> GetRunningProcesses()
	{
		std::vector<RunningProcess> processes;
		wil::unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
		if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE)
		{
			return processes;
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
				processes.push_back({entry.th32ProcessID, ToLower(std::wstring_view(buffer, length))});
			}
		}
		return processes;
	}

	std::vector<unsigned long> FindAppProcesses(
		const std::vector<RunningProcess>& processes, const std::filesystem::path& path, bool isFolder)
	{
		std::wstring target = ToLower(path.lexically_normal().wstring());
		if (isFolder)
		{
			while (!target.empty() && target.back() == L'\\')
			{
				target.pop_back();
			}
			target += L'\\'; // "C:\Apps\Foo" must not match a process in "C:\Apps\FooBar"
		}
		std::vector<unsigned long> ids;
		for (const RunningProcess& process : processes)
		{
			if (isFolder ? process.image.starts_with(target) : process.image == target)
			{
				ids.push_back(process.id);
			}
		}
		return ids;
	}
}
