#include "AppList.h"

#include "NameTokens.h"
#include "PathResolver.h"
#include "StringUtil.h"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace apptray
{
	namespace
	{
		constexpr std::int64_t SecondsPerDay = 24 * 60 * 60;

		bool IsLessIgnoreCase(const std::wstring& a, const std::wstring& b)
		{
			return CompareStringEx(
					   LOCALE_NAME_USER_DEFAULT,
					   LINGUISTIC_IGNORECASE | SORT_DIGITSASNUMBERS,
					   a.c_str(),
					   static_cast<int>(a.size()),
					   b.c_str(),
					   static_cast<int>(b.size()),
					   nullptr,
					   nullptr,
					   0)
				== CSTR_LESS_THAN;
		}

		bool IsNewEntry(const AppEntry& entry, const Catalog& catalog, std::optional<std::int64_t> now)
		{
			if (entry.firstSeen.empty() || entry.firstSeen == catalog.baselineAt || !now)
			{
				return false;
			}
			const std::optional<std::int64_t> seen = ParseUtcSeconds(entry.firstSeen);
			return seen && *now - *seen <= NewAppDays * SecondsPerDay;
		}

		std::vector<std::wstring> SplitSearchWords(std::wstring_view search)
		{
			std::vector<std::wstring> words;
			std::wstring current;
			for (const wchar_t c : FoldForSearch(search))
			{
				if (c == L' ' || c == L'\t')
				{
					if (!current.empty())
					{
						words.push_back(std::move(current));
						current.clear();
					}
				}
				else
				{
					current.push_back(c);
				}
			}
			if (!current.empty())
			{
				words.push_back(std::move(current));
			}
			return words;
		}

		// First letters of the words of a name: with camelCase humps split ("FastStone Image
		// Viewer" -> "fsiv") and without ("fiv"); variants of one letter are left out
		std::wstring MakeInitials(const std::wstring& name)
		{
			std::wstring result;
			for (const bool splitCamelCase : {true, false})
			{
				std::wstring letters;
				for (const std::wstring& word : SplitNameWords(name, splitCamelCase))
				{
					letters.push_back(word.front());
				}
				letters = FoldForSearch(letters);
				if (letters.size() >= 2 && letters != result)
				{
					result += (result.empty() ? L"" : L" ") + letters;
				}
			}
			return result;
		}

		// A word that starts one of the space-separated words of a list
		bool StartsAnyWord(std::wstring_view words, std::wstring_view word)
		{
			if (word.empty())
			{
				return false;
			}
			size_t start = 0;
			while (start < words.size())
			{
				size_t end = words.find(L' ', start);
				if (end == std::wstring_view::npos)
				{
					end = words.size();
				}
				if (words.substr(start, end - start).starts_with(word))
				{
					return true;
				}
				start = end + 1;
			}
			return false;
		}

		// A search word of two letters or more that starts one of the initials variants
		bool MatchesInitials(std::wstring_view initials, std::wstring_view word)
		{
			return word.size() >= 2 && StartsAnyWord(initials, word);
		}

		// Keywords as typed ("retoque, fotos") -> "retoque fotos"
		std::wstring FoldKeywords(std::wstring_view keywords)
		{
			std::wstring spaced(keywords);
			std::replace_if(spaced.begin(), spaced.end(), [](wchar_t c) { return c == L',' || c == L';' || c == L'\t'; }, L' ');
			std::wstring folded;
			for (const wchar_t c : FoldForSearch(spaced))
			{
				if (c != L' ' || (!folded.empty() && folded.back() != L' '))
				{
					folded.push_back(c);
				}
			}
			while (!folded.empty() && folded.back() == L' ')
			{
				folded.pop_back();
			}
			return folded;
		}

		bool IsInCategory(const std::wstring& category, const std::wstring& path)
		{
			if (path.empty())
			{
				return true;
			}
			if (category.size() < path.size()
				|| CompareStringOrdinal(category.c_str(), static_cast<int>(path.size()), path.c_str(), -1, TRUE)
					!= CSTR_EQUAL)
			{
				return false;
			}
			return category.size() == path.size() || category[path.size()] == L'/';
		}

		bool MatchesKind(const AppItem& item, const AppFilter& filter)
		{
			switch (filter.kind)
			{
			case AppFilterKind::All:
				return !item.hidden && !item.hiddenByDefault;
			case AppFilterKind::Favorites:
				return item.favorite;
			case AppFilterKind::Recent:
				return !item.lastLaunched.empty() && !item.hidden && !item.hiddenByDefault && !item.entry->missing;
			case AppFilterKind::New:
				return item.isNew && !item.hidden && !item.hiddenByDefault;
			case AppFilterKind::Autostart:
				// As PlanAutostart, but missing apps stay (marked "not found") so they can be turned off
				return item.autostart != AutostartMode::No && !item.hidden && !item.target.empty();
			case AppFilterKind::Category:
				return !item.hidden && !item.hiddenByDefault && IsInCategory(item.category, filter.category);
			case AppFilterKind::Installers:
				return !item.hidden && item.hiddenByDefault;
			case AppFilterKind::Hidden:
				return item.hidden;
			}
			return false;
		}
	}

	std::vector<AppItem> BuildAppItems(
		const Catalog& catalog, const UserSettings& settings, std::wstring_view now, const UsageStats* usage)
	{
		const std::optional<std::int64_t> nowSeconds = ParseUtcSeconds(now);
		std::vector<AppItem> items;
		items.reserve(catalog.apps.size());
		for (const AppEntry& entry : catalog.apps)
		{
			AppItem item;
			item.entry = &entry;
			item.name = entry.name;
			item.category = entry.category;
			item.target = entry.target;
			item.favorite = settings.IsFavorite(entry.id);
			item.hiddenByDefault = entry.kind == AppKind::Installer || entry.alternate;
			item.isNew = IsNewEntry(entry, catalog, nowSeconds);
			if (const AppOverride* found = settings.FindOverride(entry.id))
			{
				const AppOverride& user = *found;
				item.hidden = user.hidden;
				item.name = user.name.value_or(item.name);
				if (user.category)
				{
					item.category = NormalizeRelative(*user.category); // "Media\Audio\" -> "Media/Audio"
				}
				item.target = user.target.value_or(item.target);
				item.args = user.args.value_or(L"");
				item.keywords = FoldKeywords(user.keywords.value_or(L""));
				item.console = user.console;
				item.autostart = user.autostart;
				item.askDefault = user.askDefault;
				item.elevatedTask = user.elevatedTask;
			}
			if (const AppUsage* used = usage != nullptr ? usage->Find(entry.id) : nullptr)
			{
				item.launchCount = used->count;
				item.lastLaunched = used->lastLaunched;
			}
			item.searchText = FoldForSearch(item.name + L" " + entry.id + L" " + entry.description + L" " + item.category);
			if (!item.keywords.empty())
			{
				item.searchText += L" " + item.keywords;
			}
			item.initials = MakeInitials(item.name);
			items.push_back(std::move(item));
		}
		return items;
	}

	std::vector<size_t> FilterAppItems(
		const std::vector<AppItem>& items, const AppFilter& filter, const std::vector<std::wstring>& favorites)
	{
		const std::vector<std::wstring> words = SplitSearchWords(filter.search);
		std::vector<size_t> result;
		for (size_t i = 0; i < items.size(); ++i)
		{
			const AppItem& item = items[i];
			if (!MatchesKind(item, filter))
			{
				continue;
			}
			const bool matchesSearch = std::all_of(
				words.begin(), words.end(), [&](const std::wstring& w)
				{ return item.searchText.find(w) != std::wstring::npos || MatchesInitials(item.initials, w); });
			if (matchesSearch)
			{
				result.push_back(i);
			}
		}

		if (filter.kind == AppFilterKind::Favorites && words.empty())
		{
			std::unordered_map<std::wstring, size_t> order; // ids compared like paths
			for (size_t i = 0; i < favorites.size(); ++i)
			{
				order.emplace(ToLower(NormalizeRelative(favorites[i])), i);
			}
			const auto rank = [&](size_t index)
			{
				const auto found = order.find(ToLower(items[index].entry->id));
				return found == order.end() ? favorites.size() : found->second;
			};
			std::stable_sort(result.begin(), result.end(), [&](size_t a, size_t b) { return rank(a) < rank(b); });
			return result;
		}

		if (filter.kind == AppFilterKind::Recent && words.empty())
		{
			// ISO timestamps sort by time
			std::stable_sort(result.begin(), result.end(), [&](size_t a, size_t b)
				{ return items[a].lastLaunched > items[b].lastLaunched; });
			if (result.size() > RecentAppLimit)
			{
				result.resize(RecentAppLimit);
			}
			return result;
		}

		struct Ranked
		{
			size_t index = 0;
			int group = 0;    // Autostart without a search: 0 "always", 1 "ask"
			int match = 0;    // 2: the name (searchText does) or a keyword starts with the first word; 1: initials
			int launches = 0; // only counts while searching
		};
		const std::wstring first = words.empty() ? std::wstring{} : words.front();
		std::vector<Ranked> ranked;
		ranked.reserve(result.size());
		for (const size_t index : result)
		{
			const AppItem& item = items[index];
			const int match = first.empty()                                                    ? 0
				: item.searchText.starts_with(first) || StartsAnyWord(item.keywords, first) ? 2
				: MatchesInitials(item.initials, first)                                       ? 1
				                                                                              : 0;
			const int group = filter.kind == AppFilterKind::Autostart && words.empty()
					&& item.autostart == AutostartMode::Ask
				? 1
				: 0;
			ranked.push_back({index, group, match, first.empty() ? 0 : item.launchCount});
		}
		std::stable_sort(ranked.begin(), ranked.end(), [&](const Ranked& a, const Ranked& b)
			{
				if (a.group != b.group)
				{
					return a.group < b.group;
				}
				if (a.match != b.match)
				{
					return a.match > b.match;
				}
				if (a.launches != b.launches)
				{
					return a.launches > b.launches;
				}
				return IsLessIgnoreCase(items[a.index].name, items[b.index].name);
			});
		for (size_t i = 0; i < ranked.size(); ++i)
		{
			result[i] = ranked[i].index;
		}
		return result;
	}

	CategoryNode BuildCategoryTree(const std::vector<AppItem>& items, int maxDepth)
	{
		CategoryNode root;
		for (const AppItem& item : items)
		{
			if (item.hidden || item.hiddenByDefault || item.category.empty())
			{
				continue;
			}
			++root.count;
			CategoryNode* node = &root;
			size_t start = 0;
			for (int level = 1; start <= item.category.size() && (maxDepth <= 0 || level <= maxDepth); ++level)
			{
				const size_t slash = item.category.find(L'/', start);
				const size_t end = slash == std::wstring::npos ? item.category.size() : slash;
				const std::wstring name = item.category.substr(start, end - start);
				const std::wstring path = item.category.substr(0, end);
				auto child = std::find_if(node->children.begin(), node->children.end(), [&](const CategoryNode& c)
					{ return CompareStringOrdinal(c.path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL; });
				if (child == node->children.end())
				{
					node->children.push_back({path, name, 0, {}});
					child = std::prev(node->children.end());
				}
				++child->count;
				node = &*child;
				if (slash == std::wstring::npos)
				{
					break;
				}
				start = slash + 1;
			}
		}

		const auto sortTree = [](auto& self, CategoryNode& node) -> void
		{
			std::sort(node.children.begin(), node.children.end(), [](const CategoryNode& a, const CategoryNode& b)
				{ return IsLessIgnoreCase(a.name, b.name); });
			for (CategoryNode& child : node.children)
			{
				self(self, child);
			}
		};
		sortTree(sortTree, root);
		return root;
	}

	bool MatchesCategorySearch(std::wstring_view path, std::wstring_view search)
	{
		// Levels are words on both sides: "media/audio" and "media audio" search the same
		std::wstring levels(search);
		std::replace(levels.begin(), levels.end(), L'/', L' ');
		std::replace(levels.begin(), levels.end(), L'\\', L' ');
		const std::vector<std::wstring> words = SplitSearchWords(levels);
		if (words.empty())
		{
			return true;
		}
		std::wstring folded = FoldForSearch(path);
		std::replace(folded.begin(), folded.end(), L'/', L' ');
		return std::all_of(
			words.begin(), words.end(), [&](const std::wstring& word) { return folded.find(word) != std::wstring::npos; });
	}

	namespace
	{
		std::optional<CategoryNode> PruneCategory(const CategoryNode& node, std::wstring_view search)
		{
			if (MatchesCategorySearch(node.path, search))
			{
				return node; // with everything below it
			}
			CategoryNode kept{node.path, node.name, node.count, {}};
			for (const CategoryNode& child : node.children)
			{
				if (std::optional<CategoryNode> prunedChild = PruneCategory(child, search))
				{
					kept.children.push_back(std::move(*prunedChild));
				}
			}
			if (kept.children.empty())
			{
				return std::nullopt;
			}
			return kept;
		}
	}

	CategoryNode FilterCategoryTree(const CategoryNode& tree, std::wstring_view search)
	{
		if (SplitSearchWords(search).empty())
		{
			return tree;
		}
		CategoryNode result{tree.path, tree.name, tree.count, {}};
		for (const CategoryNode& child : tree.children)
		{
			if (std::optional<CategoryNode> kept = PruneCategory(child, search))
			{
				result.children.push_back(std::move(*kept));
			}
		}
		return result;
	}

	size_t CountAppItems(const std::vector<AppItem>& items, AppFilterKind kind)
	{
		AppFilter filter;
		filter.kind = kind;
		const size_t count = static_cast<size_t>(
			std::count_if(items.begin(), items.end(), [&](const AppItem& item) { return MatchesKind(item, filter); }));
		return kind == AppFilterKind::Recent ? std::min(count, RecentAppLimit) : count;
	}

	std::optional<ProcessScope> GetProcessScope(const AppItem& item)
	{
		if (item.target.empty())
		{
			return std::nullopt;
		}
		if (item.entry->kind == AppKind::Folder || item.entry->kind == AppKind::PortableApps)
		{
			return ProcessScope{item.entry->id, true};
		}
		return ProcessScope{item.target, false};
	}

	AutostartPlan PlanAutostart(const std::vector<AppItem>& items, const std::function<bool(const AppItem&)>& isRunning)
	{
		AutostartPlan plan;
		for (size_t i = 0; i < items.size(); ++i)
		{
			const AppItem& item = items[i];
			if (item.autostart == AutostartMode::No || item.hidden || item.entry->missing || item.target.empty()
				|| isRunning(item))
			{
				continue;
			}
			(item.autostart == AutostartMode::Always ? plan.open : plan.ask).push_back(i);
		}
		return plan;
	}
}
