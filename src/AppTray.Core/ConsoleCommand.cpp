#include "ConsoleCommand.h"

#include "CommandLine.h"
#include "StringUtil.h"

#include <windows.h>

#include <format>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		fs::path GetSystemFolder()
		{
			wchar_t buffer[MAX_PATH]{};
			const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
			return length > 0 && length < MAX_PATH ? fs::path(std::wstring(buffer, length)) : fs::path(L"C:\\Windows\\System32");
		}

		bool IsExistingFile(const fs::path& file)
		{
			// Not std::filesystem: Store apps' execution aliases (WindowsApps\pwsh.exe) are reparse
			// points it cannot follow, yet they run
			const DWORD attributes = GetFileAttributesW(file.c_str());
			return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
		}

		// Only the folders of PATH: not the current directory, where anything could sit
		std::optional<fs::path> FindInPath(std::wstring_view fileName)
		{
			const DWORD size = GetEnvironmentVariableW(L"Path", nullptr, 0);
			if (size == 0)
			{
				return std::nullopt;
			}
			std::wstring path(size, L'\0');
			path.resize(GetEnvironmentVariableW(L"Path", path.data(), size));
			size_t start = 0;
			while (start <= path.size())
			{
				size_t end = path.find(L';', start);
				if (end == std::wstring::npos)
				{
					end = path.size();
				}
				std::wstring folder = Trim(std::wstring_view(path).substr(start, end - start));
				if (folder.size() >= 2 && folder.front() == L'"' && folder.back() == L'"')
				{
					folder = folder.substr(1, folder.size() - 2);
				}
				const fs::path candidate = fs::path(folder) / fileName;
				if (!folder.empty() && candidate.is_absolute() && IsExistingFile(candidate))
				{
					return candidate;
				}
				start = end + 1;
			}
			return std::nullopt;
		}

		// A PowerShell literal: inside single quotes only quotes are special (doubled). PowerShell
		// also takes the typographic single quotes as quotes.
		std::wstring QuoteForPowerShell(std::wstring_view text)
		{
			std::wstring quoted = L"'";
			for (const wchar_t c : text)
			{
				quoted.push_back(c);
				if (c == L'\'' || c == L'\x2018' || c == L'\x2019' || c == L'\x201A' || c == L'\x201B')
				{
					quoted.push_back(c);
				}
			}
			quoted.push_back(L'\'');
			return quoted;
		}

		// Arguments written for a plain command line, made safe for cmd's own parsing: its
		// operators outside quotes are escaped
		std::wstring EscapeForCmd(std::wstring_view arguments)
		{
			std::wstring escaped;
			bool inQuotes = false;
			for (const wchar_t c : arguments)
			{
				if (c == L'"')
				{
					inQuotes = !inQuotes;
				}
				else if (!inQuotes && (c == L'&' || c == L'|' || c == L'<' || c == L'>' || c == L'^'))
				{
					escaped.push_back(L'^');
				}
				escaped.push_back(c);
			}
			return escaped;
		}
	}

	ConsoleShell ParseConsoleShell(std::wstring_view setting)
	{
		return EqualsIgnoreCase(Trim(setting), L"cmd") ? ConsoleShell::Cmd : ConsoleShell::PowerShell;
	}

	fs::path FindShellProgram(ConsoleShell shell)
	{
		const fs::path system = GetSystemFolder();
		if (shell == ConsoleShell::Cmd)
		{
			return system / L"cmd.exe";
		}
		if (std::optional<fs::path> pwsh = FindInPath(L"pwsh.exe"))
		{
			return *pwsh;
		}
		wchar_t programFiles[MAX_PATH]{};
		const DWORD length = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
		if (length > 0 && length < MAX_PATH)
		{
			const fs::path installed = fs::path(programFiles) / L"PowerShell" / L"7" / L"pwsh.exe";
			if (IsExistingFile(installed))
			{
				return installed;
			}
		}
		return system / L"WindowsPowerShell" / L"v1.0" / L"powershell.exe";
	}

	ConsoleCommand BuildConsoleCommand(
		ConsoleShell shell,
		const fs::path& shellProgram,
		const fs::path& directory,
		const std::optional<fs::path>& program,
		std::wstring_view arguments)
	{
		const std::wstring folder = directory.wstring();
		ConsoleCommand command;
		command.program = shellProgram;
		if (shell == ConsoleShell::Cmd)
		{
			// /s: cmd drops the first and the last quote and runs the rest as typed. pushd, not cd:
			// it also enters a network folder (through a temporary drive letter).
			std::wstring line = std::format(L"set \"PATH={};%PATH%\" & pushd \"{}\"", folder, folder);
			if (program)
			{
				line += std::format(L" & \"{}\"", program->wstring());
				if (!arguments.empty())
				{
					line += L' ';
					line += EscapeForCmd(arguments);
				}
			}
			command.parameters = L"/s /k \"" + line + L"\"";
			return command;
		}
		std::wstring script = std::format(
			L"$env:Path = {} + $env:Path; Set-Location -LiteralPath {}", QuoteForPowerShell(folder + L";"),
			QuoteForPowerShell(folder));
		if (program)
		{
			script += L"; & " + QuoteForPowerShell(program->wstring());
			if (!arguments.empty())
			{
				// Stop-parsing token: the arguments reach the program as written, not as PowerShell
				script += L" --% ";
				script += arguments;
			}
		}
		command.parameters = L"-NoExit -NoLogo -Command " + QuoteArgument(script);
		return command;
	}
}
