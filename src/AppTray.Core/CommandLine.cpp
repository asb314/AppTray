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
		return L"AppTray.exe [--scan [--report file.md] [--verbose]] [--root folder] [--data folder]";
	}
}
