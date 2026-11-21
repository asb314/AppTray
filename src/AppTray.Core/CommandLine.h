#pragma once

#include <optional>
#include <string>
#include <vector>

namespace apptray
{
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
}
