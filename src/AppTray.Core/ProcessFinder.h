#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace apptray
{
	struct RunningProcess
	{
		unsigned long id = 0;
		std::wstring image; // full path of its executable, lowercased
	};

	// The processes running now (one snapshot). Elevated processes are included:
	// PROCESS_QUERY_LIMITED_INFORMATION is enough to read their path.
	std::vector<RunningProcess> GetRunningProcesses();

	// Ids of the processes of an app: for a folder app, any process started from inside its folder
	// (so FirefoxPortable.exe launching App\Firefox64\firefox.exe counts, with no process list to
	// maintain); for a single file, that exact file.
	std::vector<unsigned long> FindAppProcesses(
		const std::vector<RunningProcess>& processes, const std::filesystem::path& path, bool isFolder);
}
