#include "CatalogMerger.h"

#include "NameTokens.h"
#include "PathResolver.h"
#include "StringUtil.h"

#include <windows.h>

#include <algorithm>
#include <format>
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

		// The same for two versions of an app in renamed folders: category, kind, backup flag and
		// the name without its numbers ("QGIS 4.0.0 light" and "QGIS 4.1.0 light" -> "qgislight").
		// Empty when the name has no words left.
		std::wstring MakeLineageKey(const AppEntry& entry)
		{
			const std::wstring name = NormalizeName(StripLaunchExtension(GetLeafOfRelative(entry.id)));
			if (name.empty())
			{
				return {};
			}
			return std::format(
				L"{}|{}|{}|{}", MakeKey(entry.category), ToString(entry.kind), entry.alternate ? L"copy" : L"", name);
		}

		// Another executable chosen inside the old folder, moved to the new one. Dropped when the new
		// folder has no such file among its candidates, or when it is AppTray's own choice anyway.
		std::optional<std::wstring> RebaseTarget(const std::wstring& target, const std::wstring& from, const AppEntry& entry)
		{
			const std::wstring normalized = NormalizeRelative(target);
			const std::wstring prefix = NormalizeRelative(from) + L"/";
			if (!StartsWithIgnoreCase(normalized, prefix))
			{
				return target; // outside the old folder: still valid
			}
			const std::wstring moved = NormalizeRelative(entry.id) + L"/" + normalized.substr(prefix.size());
			const bool isAlternative = std::any_of(
				entry.alternatives.begin(), entry.alternatives.end(),
				[&](const std::wstring& alternative) { return IsSameRelative(alternative, moved); });
			if (isAlternative && !IsSameRelative(moved, entry.target))
			{
				return moved;
			}
			return std::nullopt;
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
		std::vector<size_t> unknown;             // entries of catalog.apps not in the previous catalog
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
				// User data that did not move yet (user.json was broken) keeps its pointer
				const std::wstring& renamedFrom = old->second->renamedFrom;
				if (!renamedFrom.empty() && settings.HasUserData(renamedFrom))
				{
					entry.renamedFrom = renamedFrom;
				}
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
			else
			{
				unknown.push_back(catalog.apps.size());
			}
			entry.firstSeen = firstSeen.empty() ? now : firstSeen;
			entry.missing = false;
			found.insert(key);
			catalog.apps.push_back(std::move(entry));
		}

		if (previous)
		{
			// Gone from where it was: not found again, and not gone on purpose (an expanded suite,
			// whose programs replace it, or the programs of a suite grouped again)
			const auto isGone = [&](const AppEntry& entry)
			{
				const bool regrouped = settings.IsExpanded(entry.id) || suites.contains(MakeKey(entry.category));
				return !found.contains(MakeKey(entry.id)) && !regrouped;
			};

			// A new version in a renamed folder: exactly one app gone and one unknown with the same
			// lineage. Anything less clear-cut stays a new app plus a missing one.
			std::unordered_map<std::wstring, std::vector<const AppEntry*>> goneByLineage;
			for (const AppEntry& entry : previous->apps)
			{
				if (isGone(entry))
				{
					if (std::wstring lineage = MakeLineageKey(entry); !lineage.empty())
					{
						goneByLineage[lineage].push_back(&entry);
					}
				}
			}
			std::unordered_map<std::wstring, std::vector<size_t>> unknownByLineage;
			for (const size_t index : unknown)
			{
				if (std::wstring lineage = MakeLineageKey(catalog.apps[index]); !lineage.empty())
				{
					unknownByLineage[lineage].push_back(index);
				}
			}
			std::unordered_set<std::wstring> renamedAway; // keys of old entries that live on
			for (const auto& [lineage, indices] : unknownByLineage)
			{
				const auto gone = goneByLineage.find(lineage);
				if (indices.size() != 1 || gone == goneByLineage.end() || gone->second.size() != 1)
				{
					continue;
				}
				const AppEntry& old = *gone->second.front();
				AppEntry& entry = catalog.apps[indices.front()];
				if (!old.firstSeen.empty())
				{
					entry.firstSeen = old.firstSeen;
				}
				// Two updates in a row before the data moved: it still sits under the oldest id
				const bool dataStillOlder = !old.renamedFrom.empty() && !settings.HasUserData(old.id)
					&& settings.HasUserData(old.renamedFrom);
				entry.renamedFrom = dataStillOlder ? old.renamedFrom : old.id;
				renamedAway.insert(MakeKey(old.id));
			}

			for (const AppEntry& entry : previous->apps)
			{
				if (isGone(entry) && !renamedAway.contains(MakeKey(entry.id)) && settings.HasUserData(entry.id))
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

	std::vector<RenamedApp> MoveRenamedAppSettings(UserSettings& settings, const Catalog& catalog)
	{
		std::vector<RenamedApp> moved;
		for (const AppEntry& entry : catalog.apps)
		{
			const std::wstring& from = entry.renamedFrom;
			if (from.empty() || entry.missing || IsSameRelative(from, entry.id) || !settings.HasUserData(from))
			{
				continue;
			}
			RenamedApp change{from, entry.id, entry.name, false};

			// Favorite: same place in the list
			const auto favorite = std::find_if(
				settings.favorites.begin(), settings.favorites.end(),
				[&](const std::wstring& id) { return IsSameRelative(id, from); });
			if (favorite != settings.favorites.end())
			{
				if (settings.IsFavorite(entry.id))
				{
					settings.favorites.erase(favorite);
				}
				else
				{
					*favorite = entry.id;
				}
			}

			// Override, and the keys the user wrote by hand next to it in user.json
			const auto old = std::find_if(
				settings.apps.begin(), settings.apps.end(), [&](const auto& pair) { return IsSameRelative(pair.first, from); });
			if (old != settings.apps.end())
			{
				AppOverride item = old->second;
				const std::string oldKey = ToUtf8(old->first);
				settings.apps.erase(old);
				if (const AppOverride* existing = settings.FindOverride(entry.id))
				{
					// The new folder's own settings win; its task, if any, is its own
					change.elevatedTaskDropped = item.elevatedTask && !existing->elevatedTask;
				}
				else
				{
					if (item.target)
					{
						item.target = RebaseTarget(*item.target, from, entry);
					}
					if (item.elevatedTask)
					{
						item.elevatedTask = false;
						change.elevatedTaskDropped = true;
					}
					if (!item.IsEmpty())
					{
						settings.apps[entry.id] = std::move(item);
					}
					if (settings.document.is_object() && settings.document.contains("apps"))
					{
						Json& storedApps = settings.document["apps"];
						const std::string newKey = ToUtf8(entry.id);
						if (storedApps.is_object() && storedApps.contains(oldKey) && !storedApps.contains(newKey))
						{
							storedApps[newKey] = std::move(storedApps[oldKey]);
							storedApps.erase(oldKey);
						}
					}
				}
			}
			moved.push_back(std::move(change));
		}
		return moved;
	}
}
