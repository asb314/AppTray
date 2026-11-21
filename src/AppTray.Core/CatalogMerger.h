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
	// - an app whose folder was renamed for a new version ("QGIS 4.0.0 light" -> "QGIS 4.1.0
	//   light": same category, kind and name apart from the numbers, one of each) is the same app:
	//   it keeps its firstSeen and records renamedFrom, so the tray moves the user's data to it;
	// - apps gone from disk are kept as missing only if the user has data for them
	//   (favorite or override); otherwise they are dropped;
	// - apps are sorted by category and name so the file diffs stay small.
	Catalog MergeCatalog(
		std::vector<AppEntry> scanned,
		const std::optional<Catalog>& previous,
		const UserSettings& settings,
		const std::wstring& root,
		const std::wstring& now);

	// What MoveRenamedAppSettings did for one app
	struct RenamedApp
	{
		std::wstring from; // old id
		std::wstring to;   // new id
		std::wstring name;
		bool elevatedTaskDropped = false; // "Abrir sin UAC" has to be turned on again
	};

	// Moves favorites and overrides from renamedFrom to the new id of each app (the scan cannot:
	// only the tray writes user.json). Another executable chosen inside the old folder follows to
	// the same file in the new one, or is dropped. The elevated task is dropped: it runs the old
	// executable under the old name (the next elevated task change deletes it). If the new id has
	// an override already, that one wins.
	std::vector<RenamedApp> MoveRenamedAppSettings(UserSettings& settings, const Catalog& catalog);
}
