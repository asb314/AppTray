#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	// Exit codes of "AppTray.exe --scan" (the tray reads them when it refreshes)
	inline constexpr int ExitCodeScanFailed = 1;
	inline constexpr int ExitCodeBadArguments = 2;
	inline constexpr int ExitCodeRootMissing = 3;

	struct CommandLineOptions
	{
		bool scan = false;                // scan, write catalog.json (and a report), exit
		bool startup = false;             // started at logon
		bool show = false;                // open the launcher of the running instance
		bool exit = false;                // close the running instance
		bool verbose = false;             // log every folder and executable the scan touches
		std::optional<std::wstring> root; // scan root override
		std::optional<std::wstring> data; // data folder override (catalog.json, user.json, log)
		std::optional<std::wstring> report;
		std::vector<std::wstring> errors; // unknown arguments and options missing their value
	};

	// arguments excludes the program name. A value never starts with "--": "--report --verbose"
	// is an error for --report, not a report file named "--verbose".
	CommandLineOptions ParseCommandLine(const std::vector<std::wstring>& arguments);

	// Splits the process command line (GetCommandLineW) and drops the program name
	std::vector<std::wstring> GetProcessArguments();

	// Usage text shown with argument errors
	std::wstring GetUsageText();

	// Quotes one argument so CommandLineToArgvW reads it back unchanged: backslashes before a
	// quote are doubled, so "D:\" does not swallow its closing quote
	std::wstring QuoteArgument(std::wstring_view argument);
}
