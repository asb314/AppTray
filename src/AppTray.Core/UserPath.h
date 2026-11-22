#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// The user's PATH (HKCU\Environment "Path"): AppTray adds an app's folder so its command-line
	// programs work from any console, and takes it out again. Entries are compared ignoring case,
	// surrounding quotes, a trailing backslash and %variables% (expanded).

	bool ContainsPathEntry(std::wstring_view list, std::wstring_view directory);

	// Appended at the end, unless it is already there
	std::wstring AddPathEntry(std::wstring_view list, std::wstring_view directory);

	// Every copy of it
	std::wstring RemovePathEntry(std::wstring_view list, std::wstring_view directory);

	// Entries that are the folder or inside it point into the new folder instead (an app folder
	// renamed for a new version: "...\ffmpeg 7.1\bin" -> "...\ffmpeg 8.0\bin")
	std::wstring RebasePathEntries(std::wstring_view list, std::wstring_view from, std::wstring_view to);

	struct UserPath
	{
		std::wstring list;
		bool expandable = true; // REG_EXPAND_SZ (Windows' default) or REG_SZ
	};

	// key: under HKEY_CURRENT_USER (tests use their own). A missing value reads as empty.
	std::optional<UserPath> ReadUserPath(std::wstring& error, const wchar_t* key = L"Environment");
	bool WriteUserPath(const UserPath& path, std::wstring& error, const wchar_t* key = L"Environment");

	// Tells running programs (Explorer, terminals) that the environment changed, so the consoles
	// they open next get the new PATH. Waits up to a few seconds for windows to answer: call it
	// off the UI thread.
	void AnnounceEnvironmentChange();

	// This process's PATH read again from the registry (system plus user), so the programs AppTray
	// starts see a change made after it started
	bool RefreshProcessPath();
}
