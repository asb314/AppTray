#include "CatalogMerger.h"

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
		if (previous)
		{
			for (const AppEntry& entry : previous->apps)
			{
				known.emplace(entry.id, &entry);
			}
		}

		std::unordered_set<std::wstring> found;
		for (AppEntry& entry : scanned)
		{
			const auto old = known.find(entry.id);
			entry.firstSeen = old != known.end() && !old->second->firstSeen.empty() ? old->second->firstSeen : now;
			entry.missing = false;
			found.insert(entry.id);
			catalog.apps.push_back(std::move(entry));
		}

		if (previous)
		{
			for (const AppEntry& entry : previous->apps)
			{
				if (!found.contains(entry.id) && settings.HasUserData(entry.id))
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
