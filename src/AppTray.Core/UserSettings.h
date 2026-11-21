#pragma once

#include "JsonFile.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace apptray
{
	enum class AutostartMode
	{
		No,
		Always, // opened at logon if not already running
		Ask,    // offered in the logon dialog with a countdown
	};

	// What the user changed about one app. Keyed by the app id in user.json.
	struct AppOverride
	{
		bool hidden = false;
		std::optional<std::wstring> name;
		std::optional<std::wstring> category;
		std::optional<std::wstring> target; // another executable, relative to the root
		std::optional<std::wstring> args;
		bool expand = false;                // a suite listed as separate apps
		AutostartMode autostart = AutostartMode::No;
		bool askDefault = false;            // checked in the logon dialog
		bool elevatedTask = false;          // started through its own elevated scheduled task

		bool IsEmpty() const;
	};

	struct ScanSettings
	{
		int maxDepth = 6;
		std::vector<std::wstring> ignore{L".claude", L"temp", L"Applications"};
		std::vector<std::wstring> skipNames;
	};

	// user.json: everything the user decides. The scanner never writes it; unknown keys the user
	// adds by hand survive a save.
	struct UserSettings
	{
		std::optional<std::wstring> root; // relative to AppTray's folder or absolute; default ".."
		bool startWithWindows = false;
		int startupDelaySeconds = 30;
		int startupPromptSeconds = 15;
		int categoryDepth = 0; // levels of folders shown as categories: 0 = all, 1 = top level only
		ScanSettings scan;
		std::vector<std::wstring> favorites;
		std::map<std::wstring, AppOverride> apps;
		Json document = Json::object(); // the file as read, to keep unknown keys

		bool IsFavorite(const std::wstring& id) const;
		bool HasUserData(const std::wstring& id) const;
		std::vector<std::wstring> ExpandedIds() const;

		// Override of an id, ignoring case (ids are paths); nullptr if none
		const AppOverride* FindOverride(const std::wstring& id) const;
		bool IsExpanded(const std::wstring& id) const;
	};

	// Changes made from the UI. Overrides left empty are removed.
	void ToggleFavorite(UserSettings& settings, const std::wstring& id);
	void SetHidden(UserSettings& settings, const std::wstring& id, bool hidden);
	void SetTargetOverride(UserSettings& settings, const std::wstring& id, std::optional<std::wstring> target);
	void SetExpanded(UserSettings& settings, const std::wstring& id, bool expanded);

	// A missing file gives the defaults (no error). A broken file gives the defaults and an error.
	UserSettings LoadUserSettings(const std::filesystem::path& file, std::wstring& error);
	bool SaveUserSettings(const std::filesystem::path& file, const UserSettings& settings, std::wstring& error);
}
