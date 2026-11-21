#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	enum class AppKind
	{
		Folder,       // a folder with its own program; target is the best executable inside
		File,         // a single executable lying in a category folder
		PortableApps, // PortableApps.com format, described by App\AppInfo\appinfo.ini
		Installer,    // only installers (setup.exe, *.paf.exe...): hidden by default
		Suite,        // many unrelated tools in one folder (Sysinternals): opens the folder
	};

	std::wstring_view ToString(AppKind kind);
	std::optional<AppKind> ParseAppKind(std::wstring_view text);

	// One launchable entry of the catalog. Every path is relative to the scan root and uses '/'.
	struct AppEntry
	{
		std::wstring id;          // relative path of the app folder or file; stable across scans
		AppKind kind = AppKind::Folder;
		std::wstring name;
		std::wstring description; // product name and file description of the target
		std::wstring category;    // relative path of the containing folder: "Media/Audio/Editor"
		std::wstring target;      // relative path of the executable; empty for a Suite
		std::vector<std::wstring> alternatives; // other executables of the same app
		std::wstring readme;      // readme.txt / LEEME.txt / *.chm of the app, if any
		std::wstring firstSeen;   // UTC time of the scan that found it first
		int confidence = 0;       // name match of the target with its folder, 0..100
		bool requiresAdmin = false;
		bool console = false;
		bool alternate = false;   // backup copy folder ("dbeaver_", "HandBrake_1")
		bool missing = false;     // gone from disk, kept because the user has data for it
	};
}
