#include "CommandLine.h"

#include "StringUtil.h"

#include <windows.h>

#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

namespace apptray
{
	CommandLineOptions ParseCommandLine(const std::vector<std::wstring>& arguments)
	{
		CommandLineOptions options;
		for (size_t i = 0; i < arguments.size(); ++i)
		{
			const std::wstring& argument = arguments[i];
			const auto takeValue = [&]() -> std::optional<std::wstring>
			{
				if (i + 1 < arguments.size() && !arguments[i + 1].starts_with(L"--"))
				{
					return arguments[++i];
				}
				options.errors.push_back(argument + L" needs a value");
				return std::nullopt;
			};

			if (EqualsIgnoreCase(argument, L"--scan"))
			{
				options.scan = true;
			}
			else if (EqualsIgnoreCase(argument, L"--startup"))
			{
				options.startup = true;
			}
			else if (EqualsIgnoreCase(argument, L"--show"))
			{
				options.show = true;
			}
			else if (EqualsIgnoreCase(argument, L"--exit"))
			{
				options.exit = true;
			}
			else if (EqualsIgnoreCase(argument, L"--verbose"))
			{
				options.verbose = true;
			}
			else if (EqualsIgnoreCase(argument, L"--check-update"))
			{
				options.checkUpdate = true;
			}
			else if (EqualsIgnoreCase(argument, L"--register-task"))
			{
				options.registerTask = takeValue();
			}
			else if (EqualsIgnoreCase(argument, L"--unregister-task"))
			{
				options.unregisterTask = takeValue();
			}
			else if (EqualsIgnoreCase(argument, L"--task-user"))
			{
				options.taskUser = takeValue();
			}
			else if (EqualsIgnoreCase(argument, L"--root"))
			{
				options.root = takeValue();
			}
			else if (EqualsIgnoreCase(argument, L"--data"))
			{
				options.data = takeValue();
			}
			else if (EqualsIgnoreCase(argument, L"--report"))
			{
				options.report = takeValue();
			}
			else
			{
				options.errors.push_back(L"unknown argument: " + argument);
			}
		}
		return options;
	}

	std::vector<std::wstring> GetProcessArguments()
	{
		int count = 0;
		LPWSTR* list = CommandLineToArgvW(GetCommandLineW(), &count);
		std::vector<std::wstring> arguments;
		if (list == nullptr)
		{
			return arguments;
		}
		for (int i = 1; i < count; ++i)
		{
			arguments.emplace_back(list[i]);
		}
		LocalFree(list);
		return arguments;
	}

	std::wstring GetUsageText()
	{
		return L"AppTray.exe [--show | --exit | --startup | --scan [--report file.md] [--verbose] | --check-update]"
			   L" [--root folder] [--data folder]";
	}

	std::wstring QuoteArgument(std::wstring_view argument)
	{
		std::wstring quoted = L"\"";
		size_t backslashes = 0;
		for (const wchar_t c : argument)
		{
			if (c == L'\\')
			{
				++backslashes;
				continue;
			}
			// Backslashes are literal unless a quote follows: then each must be doubled
			quoted.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
			backslashes = 0;
			quoted.push_back(c);
		}
		quoted.append(backslashes * 2, L'\\'); // before the closing quote
		quoted.push_back(L'"');
		return quoted;
	}
}
