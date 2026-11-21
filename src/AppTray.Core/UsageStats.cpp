#include "UsageStats.h"

#include "JsonFile.h"
#include "PathResolver.h"
#include "StringUtil.h"

#include <algorithm>

namespace apptray
{
	namespace
	{
		constexpr int UsageVersion = 1;
	}

	std::wstring MakeUsageKey(std::wstring_view id)
	{
		return ToLower(NormalizeRelative(id));
	}

	const AppUsage* UsageStats::Find(std::wstring_view id) const
	{
		const auto found = apps.find(MakeUsageKey(id));
		return found == apps.end() ? nullptr : &found->second;
	}

	void RecordLaunch(UsageStats& stats, std::wstring_view id, const std::wstring& now)
	{
		AppUsage& usage = stats.apps[MakeUsageKey(id)];
		++usage.count;
		usage.lastLaunched = now;
	}

	bool MoveUsage(UsageStats& stats, std::wstring_view from, std::wstring_view to)
	{
		const std::wstring fromKey = MakeUsageKey(from);
		const std::wstring toKey = MakeUsageKey(to);
		const auto old = stats.apps.find(fromKey);
		if (old == stats.apps.end() || fromKey == toKey)
		{
			return false;
		}
		const AppUsage moved = old->second;
		stats.apps.erase(old);
		AppUsage& target = stats.apps[toKey];
		target.count += moved.count;
		target.lastLaunched = std::max(target.lastLaunched, moved.lastLaunched); // ISO strings sort by time
		return true;
	}

	UsageStats LoadUsageStats(const std::filesystem::path& file)
	{
		UsageStats stats;
		std::wstring error;
		const std::optional<Json> json = ReadJsonFile(file, error);
		if (!json || !json->is_object())
		{
			return stats;
		}
		const auto apps = json->find("apps");
		if (apps == json->end() || !apps->is_object())
		{
			return stats;
		}
		for (const auto& [key, value] : apps->items())
		{
			if (!value.is_object())
			{
				continue;
			}
			AppUsage usage;
			usage.count = std::max(0, GetInt(value, "count", 0));
			usage.lastLaunched = GetString(value, "last");
			if (usage.count > 0 || !usage.lastLaunched.empty())
			{
				stats.apps[MakeUsageKey(FromUtf8(key))] = std::move(usage);
			}
		}
		return stats;
	}

	bool SaveUsageStats(const std::filesystem::path& file, const UsageStats& stats, std::wstring& error)
	{
		Json apps = Json::object();
		for (const auto& [key, usage] : stats.apps)
		{
			Json value = Json::object();
			value["count"] = usage.count;
			value["last"] = ToJson(usage.lastLaunched);
			apps[ToUtf8(key)] = std::move(value);
		}
		Json json = Json::object();
		json["version"] = UsageVersion;
		json["apps"] = std::move(apps);
		return WriteJsonFile(file, json, error);
	}
}
