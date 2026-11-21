#pragma once

#include "Catalog.h"
#include "UserSettings.h"

#include <optional>
#include <string>
#include <vector>

namespace apptray
{
	// Combines a fresh scan with the previous catalog:
	// - firstSeen is kept for known apps and set to `now` for new ones;
	// - apps gone from disk are kept as missing only if the user has data for them
	//   (favorite or override); otherwise they are dropped;
	// - apps are sorted by category and name so the file diffs stay small.
	Catalog MergeCatalog(
		std::vector<AppEntry> scanned,
		const std::optional<Catalog>& previous,
		const UserSettings& settings,
		const std::wstring& root,
		const std::wstring& now);
}
