#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace apptray
{
	struct LaunchResult
	{
		bool started = false;
		bool cancelled = false;  // the user said no to the UAC prompt
		unsigned long error = 0; // Win32 error code when not started
	};

	// Starts a program (or .bat, .lnk, .jar) through the shell, in its own folder unless directory
	// says another. asAdmin uses the "runas" verb; programs whose manifest asks for elevation get the
	// UAC prompt anyway.
	LaunchResult LaunchProgram(
		const std::filesystem::path& target, std::wstring_view arguments, bool asAdmin, const std::filesystem::path& directory = {});

	// Opens a folder or a document (readme) with its default program
	LaunchResult OpenWithShell(const std::filesystem::path& path);

	// Opens Explorer on the file's folder with the file selected
	LaunchResult RevealInExplorer(const std::filesystem::path& file);

	// System message for the error, in the user's language
	std::wstring DescribeLaunchError(const LaunchResult& result);
}
