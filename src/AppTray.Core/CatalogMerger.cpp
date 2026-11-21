#include "CatalogMerger.h"

#include "PathResolver.h"
#include "StringUtil.h"

#include <windows.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace apptray
{
	namespace
	{
		bool IsLessIgnoreCase(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
		}

		bool IsEqualIgnoreCase(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_EQUAL;
		}

		// Ids compared without case and slash style, like Windows paths
		std::wstring MakeKey(const std::wstring& id)
		{
			return ToLower(NormalizeRelative(id));
		}
	}

	Catalog MergeCatalog(
		std::vector<AppEntry> scanned,
		const std::optional<Catalog>& previous,
		const UserSettings& settings,
		const std::wstring& root,
		const std::wstring& now)
	{
		Catalog catalog;
		catalog.root = root;
		catalog.scannedAt = now;
		catalog.baselineAt = previous && !previous->baselineAt.empty() ? previous->baselineAt : now;

		std::unordered_map<std::wstring, const AppEntry*> known;
		// Earliest firstSeen of the previous entries inside each folder (by their category)
		std::unordered_map<std::wstring, std::wstring> firstSeenInside;
		if (previous)
		{
			for (const AppEntry& entry : previous->apps)
			{
				known.emplace(MakeKey(entry.id), &entry);
				std::wstring& earliest = firstSeenInside[MakeKey(entry.category)];
				if (!entry.firstSeen.empty() && (earliest.empty() || entry.firstSeen < earliest))
				{
					earliest = entry.firstSeen;
				}
			}
		}

		std::unordered_set<std::wstring> found;
		std::unordered_set<std::wstring> suites; // grouped suites in this scan
		for (const AppEntry& entry : scanned)
		{
			if (entry.kind == AppKind::Suite)
			{
				suites.insert(MakeKey(entry.id));
			}
		}
		for (AppEntry& entry : scanned)
		{
			const std::wstring key = MakeKey(entry.id);
			std::wstring firstSeen;
			if (const auto old = known.find(key); old != known.end())
			{
				firstSeen = old->second->firstSeen;
			}
			// Not new, just regrouped: the programs of a suite the user expanded inherit the
			// suite's date, and a suite grouped again inherits its programs' earliest date.
			// Anything else unknown is new, even inside a known folder.
			else if (const auto parent = known.find(MakeKey(entry.category));
					 parent != known.end() && settings.IsExpanded(entry.category))
			{
				firstSeen = parent->second->firstSeen;
			}
			else if (const auto inside = firstSeenInside.find(key);
					 inside != firstSeenInside.end() && entry.kind == AppKind::Suite)
			{
				firstSeen = inside->second;
			}
			entry.firstSeen = firstSeen.empty() ? now : firstSeen;
			entry.missing = false;
			found.insert(key);
			catalog.apps.push_back(std::move(entry));
		}

		if (previous)
		{
			for (const AppEntry& entry : previous->apps)
			{
				// Gone on purpose, not missing: an expanded suite (its programs replace it), or the
				// programs of a suite grouped again (the suite replaces them)
				const bool regrouped = settings.IsExpanded(entry.id) || suites.contains(MakeKey(entry.category));
				if (!found.contains(MakeKey(entry.id)) && !regrouped && settings.HasUserData(entry.id))
				{
					AppEntry kept = entry;
					kept.missing = true;
					catalog.apps.push_back(std::move(kept));
				}
			}
		}

		std::stable_sort(
			catalog.apps.begin(),
			catalog.apps.end(),
			[](const AppEntry& a, const AppEntry& b)
			{
				if (!IsEqualIgnoreCase(a.category, b.category))
				{
					return IsLessIgnoreCase(a.category, b.category);
				}
				return IsLessIgnoreCase(a.name, b.name);
			});
		return catalog;
	}
}
