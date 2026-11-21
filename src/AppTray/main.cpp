// AppTray entry point. Phase 1: command-line scan only (--scan [--report file.md]).
// The tray icon and the launcher window arrive in phase 2.

#include "Catalog.h"
#include "CatalogMerger.h"
#include "CommandLine.h"
#include "FolderScanner.h"
#include "Log.h"
#include "PathResolver.h"
#include "ScanReport.h"
#include "StringUtil.h"
#include "UserSettings.h"

#include <windows.h>

#include <wil/resource.h>

#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace apptray;

namespace
{
	fs::path GetExeDirectory()
	{
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length < buffer.size())
			{
				buffer.resize(length);
				break;
			}
			buffer.resize(buffer.size() * 2);
		}
		return fs::path(buffer).parent_path();
	}

	// AppTray is a GUI program: when started from a terminal, write to the parent's console
	// (or to the redirected output) so --scan can report what it did.
	class ConsoleOutput
	{
	public:
		ConsoleOutput()
		{
			// Redirected output (a pipe or file) is inherited as the standard handle
			output_ = GetStdHandle(STD_OUTPUT_HANDLE);
			if (output_ == nullptr || output_ == INVALID_HANDLE_VALUE)
			{
				output_ = nullptr;
				// A GUI program gets no standard handles: borrow the parent's console, if any
				if (AttachConsole(ATTACH_PARENT_PROCESS) || GetLastError() == ERROR_ACCESS_DENIED)
				{
					console_.reset(CreateFileW(
						L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
					output_ = console_ ? console_.get() : nullptr;
				}
			}
		}

		// false when started without a console or redirected output (double-click, shortcut)
		bool IsAvailable() const
		{
			return output_ != nullptr;
		}

		void Write(std::wstring_view text) const
		{
			if (output_ == nullptr)
			{
				return;
			}
			DWORD mode = 0;
			DWORD written = 0;
			if (GetConsoleMode(output_, &mode))
			{
				WriteConsoleW(output_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
			}
			else
			{
				const std::string utf8 = ToUtf8(text);
				WriteFile(output_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
			}
		}

	private:
		wil::unique_hfile console_;
		HANDLE output_ = nullptr;
	};

	// Argument errors go to the console when there is one, otherwise to a message box
	int ReportArgumentErrors(const std::vector<std::wstring>& errors)
	{
		std::wstring text;
		for (const std::wstring& error : errors)
		{
			text += error + L"\n";
		}
		text += L"\n" + GetUsageText() + L"\n";
		const ConsoleOutput console;
		if (console.IsAvailable())
		{
			console.Write(text);
		}
		else
		{
			MessageBoxW(nullptr, text.c_str(), L"AppTray", MB_OK | MB_ICONWARNING);
		}
		return 2;
	}

	int RunScan(const CommandLineOptions& options, const fs::path& exeDirectory, const fs::path& dataDirectory)
	{
		const ConsoleOutput console;
		const auto report = [&](const std::wstring& message)
		{
			console.Write(message + L"\n");
			WriteLog(message);
		};

		std::wstring error;
		const fs::path settingsFile = dataDirectory / L"user.json";
		std::error_code existsError;
		const bool hadSettings = fs::exists(settingsFile, existsError);
		UserSettings settings = LoadUserSettings(settingsFile, error);
		if (!error.empty())
		{
			report(L"user.json: " + error + L" (using defaults)");
		}

		const fs::path root = PathResolver::ResolveRoot(options.root, settings.root, exeDirectory);
		ScanOptions scanOptions;
		scanOptions.root = root;
		scanOptions.maxDepth = settings.scan.maxDepth;
		scanOptions.ignorePaths = settings.scan.ignore;
		scanOptions.skipNames = settings.scan.skipNames;
		scanOptions.expandIds = settings.ExpandedIds();
		scanOptions.selfDirectory = exeDirectory;
		if (options.verbose)
		{
			scanOptions.progress = [](std::wstring_view step) { WriteLog(step); };
		}
		ScanResult scan = ScanFolders(scanOptions);
		if (!scan.rootFound)
		{
			// Never replace a catalog with an empty one because a drive is unplugged
			report(std::format(L"AppTray: cannot read {} (catalog left as it was)", root.wstring()));
			return 1;
		}

		const fs::path catalogFile = dataDirectory / L"catalog.json";
		std::optional<Catalog> previous;
		if (fs::exists(catalogFile, existsError))
		{
			std::wstring catalogError;
			previous = LoadCatalog(catalogFile, catalogError);
			if (!previous)
			{
				report(L"catalog.json: " + catalogError + L" (starting a new catalog)");
			}
		}

		const Catalog catalog = MergeCatalog(std::move(scan.apps), previous, settings, root.wstring(), FormatUtcNow());
		std::wstring saveError;
		if (!SaveCatalog(catalogFile, catalog, saveError))
		{
			report(saveError);
			return 1;
		}
		if (!hadSettings && error.empty() && !SaveUserSettings(settingsFile, settings, saveError))
		{
			report(saveError);
		}
		if (options.report && !WriteScanReport(fs::absolute(*options.report), catalog, scan, saveError))
		{
			report(saveError);
			return 1;
		}

		report(std::format(
			L"AppTray: {} apps in {} ms ({} folders, {} executables inspected) -> {}",
			catalog.apps.size(),
			scan.elapsedMilliseconds,
			scan.foldersVisited,
			scan.executablesInspected,
			catalogFile.wstring()));
		return 0;
	}
}

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	const CommandLineOptions options = ParseCommandLine(GetProcessArguments());
	if (!options.errors.empty())
	{
		return ReportArgumentErrors(options.errors);
	}
	const fs::path exeDirectory = GetExeDirectory();
	const fs::path dataDirectory = options.data ? fs::absolute(*options.data) : exeDirectory / L"data";
	InitializeLog(dataDirectory / L"AppTray.log");

	if (options.scan)
	{
		return RunScan(options, exeDirectory, dataDirectory);
	}

	MessageBoxW(
		nullptr,
		L"La bandeja llega en la fase 2.\n\nPor ahora: AppTray.exe --scan [--report informe.md]",
		L"AppTray",
		MB_OK | MB_ICONINFORMATION);
	return 0;
}
