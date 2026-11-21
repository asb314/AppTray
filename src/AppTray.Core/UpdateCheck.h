#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// "Is there a newer release?" Only a notice: AppTray never downloads or replaces itself.

	struct ReleaseInfo
	{
		std::wstring version; // "0.3.1" (tag without a leading 'v')
		std::wstring url;     // release page
	};

	// Compares dotted versions numerically ("0.10.0" > "0.9.2", "v0.3" == "0.3.0"): <0, 0, >0
	int CompareVersions(std::wstring_view a, std::wstring_view b);

	// The GitHub API answer for /releases/latest (tag_name, html_url)
	std::optional<ReleaseInfo> ParseLatestRelease(std::string_view json);

	// GET https://api.github.com/repos/<repository>/releases/latest (WinHTTP, system proxy,
	// 15 s timeouts). nullopt with no error: the repository has no release yet.
	std::optional<ReleaseInfo> FetchLatestRelease(std::wstring_view repository, std::wstring& error);

	// data\update.json: what the last check found, so the tray does not ask more than once a day
	struct UpdateState
	{
		std::wstring checkedAt;      // UTC, ISO 8601
		std::wstring latestVersion;
		std::wstring url;
		std::wstring notifiedVersion; // the user was told about this one already
	};

	std::optional<UpdateState> LoadUpdateState(const std::filesystem::path& file);
	bool SaveUpdateState(const std::filesystem::path& file, const UpdateState& state, std::wstring& error);
}
