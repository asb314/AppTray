#pragma once

#include "Catalog.h"
#include "FolderScanner.h"
#include "UserSettings.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace apptray
{
	struct RefreshRequest
	{
		std::filesystem::path root;
		std::filesystem::path catalogFile;
		std::filesystem::path selfDirectory; // AppTray's folder, left out of the scan
		UserSettings settings;               // a copy: the scan may run on another thread
		std::optional<Catalog> previous;
		std::function<void(std::wstring_view)> progress;
		std::stop_token stop;
	};

	struct RefreshOutcome
	{
		bool rootFound = false; // false: nothing was written
		bool cancelled = false; // stopped on request: nothing was written
		bool saved = false;
		std::wstring error;     // why it was not saved
		Catalog catalog;        // the new catalog (valid when rootFound)
		ScanResult scan;        // statistics and skipped folders (apps moved into catalog)
		size_t newApps = 0;     // apps not in the previous catalog (0 on the first scan)
	};

	// Scans, merges with the previous catalog and saves catalog.json. Thread-safe: touches only
	// what the request carries.
	RefreshOutcome RefreshCatalog(const RefreshRequest& request);
}
