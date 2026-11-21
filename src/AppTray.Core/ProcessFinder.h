#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace apptray
{
	// Full paths of the executables running now (one snapshot), lowercased. Elevated processes
	// are included: PROCESS_QUERY_LIMITED_INFORMATION is enough to read their path.
	std::vector<std::wstring> GetRunningImagePaths();

	// Whether an app is running: for a folder app, any process started from inside its folder
	// (so FirefoxPortable.exe launching App\Firefox64\firefox.exe counts, with no process list
	// to maintain); for a single file, that exact file.
	bool IsAppRunning(const std::vector<std::wstring>& runningImages, const std::filesystem::path& path, bool isFolder);
}
