#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// Shortcuts (.lnk) to apps. Written by a child "AppTray.exe --create-shortcut": IShellLink
	// loads the shell namespace, which would stay in the tray's memory for good.
	enum class ShortcutPlace
	{
		Desktop,
		StartMenu, // Start menu\Programs\AppTray, so Windows search finds the app
	};

	std::optional<ShortcutPlace> ParseShortcutPlace(std::wstring_view text); // "desktop" | "startmenu"
	std::wstring_view ToString(ShortcutPlace place);

	// "Foo: bar?" -> "Foo bar.lnk"; Windows device names get a "_" ("Con" -> "Con_.lnk")
	std::wstring MakeShortcutFileName(std::wstring_view appName);

	// The file to write in folder: fileName if free or already a shortcut to target, else
	// "Name (2).lnk", "Name (3).lnk"... so a shortcut that opens something else is never replaced.
	// For a .ps1 target, a shortcut to it is one that opens PowerShell (pwsh.exe or
	// powershell.exe) with the script's quoted path in its arguments, as BuildScriptCommand does.
	// Empty if all are taken. Reads existing links: needs COM.
	std::filesystem::path ChooseShortcutFile(
		const std::filesystem::path& folder, std::wstring_view fileName, const std::filesystem::path& target);

	// The folder for a place (the Start menu one is created). Empty with a message on failure.
	std::filesystem::path GetShortcutFolder(ShortcutPlace place, std::wstring& error);

	struct ShortcutSpec
	{
		std::filesystem::path file;             // the .lnk to write (replaced if it exists)
		std::filesystem::path target;           // program, script or folder
		std::wstring arguments;
		std::filesystem::path workingDirectory; // empty for a folder
		std::wstring description;               // tooltip
	};

	// Needs COM initialized on the calling thread
	bool WriteShortcut(const ShortcutSpec& spec, std::wstring& error);
}
