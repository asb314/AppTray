#pragma once

#include "Catalog.h"
#include "FolderScanner.h"

#include <filesystem>
#include <string>

namespace apptray
{
	// Markdown report of a scan, to review the scanner rules against a real tree:
	// totals, apps per category with their chosen executable, low-confidence picks,
	// installers, backup copies and skipped folders.
	std::string BuildScanReport(const Catalog& catalog, const ScanResult& scan);

	bool WriteScanReport(
		const std::filesystem::path& file, const Catalog& catalog, const ScanResult& scan, std::wstring& error);
}
