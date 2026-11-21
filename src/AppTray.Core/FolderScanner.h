#pragma once

#include "AppEntry.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	struct ScanOptions
	{
		std::filesystem::path root;
		int maxDepth = 6;
		std::vector<std::wstring> ignorePaths; // relative to root ('/' or '\'), never entered
		std::vector<std::wstring> skipNames;   // extra folder names never entered, at any depth
		std::vector<std::wstring> expandIds;   // app folders whose executables become separate apps
		std::filesystem::path selfDirectory;   // AppTray's own folder, never scanned
		// Called with every folder entered and every executable inspected (diagnostics)
		std::function<void(std::wstring_view)> progress;
	};

	struct ScanResult
	{
		// false when the root is missing or cannot be listed (unplugged drive, offline share):
		// then apps is empty and must not replace an existing catalog
		bool rootFound = false;
		std::vector<AppEntry> apps;
		std::vector<std::wstring> skippedFolders; // relative paths left out by a rule (for the report)
		int foldersVisited = 0;
		int executablesInspected = 0;
		std::uint64_t elapsedMilliseconds = 0;
	};

	// Walks the tree and turns it into catalog entries. Category folders are told apart from
	// app folders by name matching, by the presence of DLLs and by what their subfolders hold
	// (see schema.md for the rules). firstSeen is left empty: CatalogMerger fills it.
	ScanResult ScanFolders(const ScanOptions& options);
}
