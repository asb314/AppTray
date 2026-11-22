#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	enum class ConsoleShell
	{
		PowerShell, // PowerShell 7 (pwsh.exe) when installed, else Windows PowerShell
		Cmd,
	};

	// A PowerShell literal: inside single quotes only quotes are special (doubled). PowerShell
	// also takes the typographic single quotes as quotes.
	std::wstring QuoteForPowerShell(std::wstring_view text);

	// user.json "shell": "cmd", or anything else for PowerShell
	ConsoleShell ParseConsoleShell(std::wstring_view setting);

	// The shell's executable: pwsh.exe from PATH or its usual folder, else Windows PowerShell;
	// cmd.exe from System32
	std::filesystem::path FindShellProgram(ConsoleShell shell);

	struct ConsoleCommand
	{
		std::filesystem::path program;
		std::wstring parameters;
	};

	// A console that opens in directory, with that folder first in PATH, and stays open. With a
	// program it runs it there first (with its arguments): a command-line tool shows its output
	// instead of a window that closes at once. It is all in the command line, so it works elevated
	// too ("runas" starts with a fresh environment and in System32). A PowerShell script (only with
	// ConsoleShell::PowerShell) gets the execution policy bypassed for that console alone, so one
	// unzipped from a download runs too, and its arguments are PowerShell syntax, as typed.
	ConsoleCommand BuildConsoleCommand(
		ConsoleShell shell,
		const std::filesystem::path& shellProgram,
		const std::filesystem::path& directory,
		const std::optional<std::filesystem::path>& program = std::nullopt,
		std::wstring_view arguments = {});

	// A .ps1 file: the shell opens it in Notepad, so AppTray runs it with BuildScriptCommand
	bool IsPowerShellScript(const std::filesystem::path& file);

	// How a .ps1 runs: in a PowerShell console in its own folder, whatever user.json "shell" says
	ConsoleCommand BuildScriptCommand(const std::filesystem::path& script, std::wstring_view arguments);
}
