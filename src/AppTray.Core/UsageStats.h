#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace apptray
{
	struct AppUsage
	{
		int count = 0;
		std::wstring lastLaunched; // UTC, ISO 8601
	};

	// data\usage.json: how often and when each app was opened from AppTray (launcher or tray
	// menu; not the logon apps), for the Recent list and to rank search results. Only the tray
	// writes it.
	struct UsageStats
	{
		std::map<std::wstring, AppUsage> apps; // key: MakeUsageKey(id)

		const AppUsage* Find(std::wstring_view id) const;
	};

	// Ids compare like paths: lowercase, '/' separators
	std::wstring MakeUsageKey(std::wstring_view id);

	void RecordLaunch(UsageStats& stats, std::wstring_view id, const std::wstring& now);

	// An app that moved to a new id (renamed folder) keeps its history. True if anything moved.
	bool MoveUsage(UsageStats& stats, std::wstring_view from, std::wstring_view to);

	// A missing or broken file gives empty stats: they only order lists
	UsageStats LoadUsageStats(const std::filesystem::path& file);
	bool SaveUsageStats(const std::filesystem::path& file, const UsageStats& stats, std::wstring& error);
}
