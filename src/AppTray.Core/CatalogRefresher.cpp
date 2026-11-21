#include "CatalogRefresher.h"

#include "CatalogMerger.h"
#include "StringUtil.h"

#include <algorithm>

namespace apptray
{
	RefreshOutcome RefreshCatalog(const RefreshRequest& request)
	{
		ScanOptions options;
		options.root = request.root;
		options.maxDepth = request.settings.scan.maxDepth;
		options.ignorePaths = request.settings.scan.ignore;
		options.skipNames = request.settings.scan.skipNames;
		options.expandIds = request.settings.ExpandedIds();
		options.selfDirectory = request.selfDirectory;
		options.progress = request.progress;
		options.stop = request.stop;

		RefreshOutcome outcome;
		outcome.scan = ScanFolders(options);
		outcome.rootFound = outcome.scan.rootFound;
		outcome.cancelled = outcome.scan.cancelled;
		if (!outcome.rootFound)
		{
			outcome.error = L"cannot read " + request.root.wstring();
			return outcome;
		}
		if (outcome.cancelled)
		{
			outcome.error = L"cancelled";
			return outcome;
		}

		const std::wstring now = FormatUtcNow();
		outcome.catalog =
			MergeCatalog(std::move(outcome.scan.apps), request.previous, request.settings, request.root.wstring(), now);
		outcome.scan.apps.clear();
		if (request.previous)
		{
			outcome.newApps = static_cast<size_t>(std::count_if(
				outcome.catalog.apps.begin(),
				outcome.catalog.apps.end(),
				[&](const AppEntry& entry) { return entry.firstSeen == now && !entry.missing; }));
		}
		outcome.saved = SaveCatalog(request.catalogFile, outcome.catalog, outcome.error);
		return outcome;
	}
}
