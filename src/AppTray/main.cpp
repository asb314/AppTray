// AppTray entry point: the tray program, or a command-line scan with --scan.

#include "Catalog.h"
#include "CatalogRefresher.h"
#include "CommandLine.h"
#include "Log.h"
#include "PathResolver.h"
#include "ScanReport.h"
#include "StringUtil.h"
#include "TrayApp.h"
#include "UiCommon.h"
#include "UserSettings.h"

#include <windows.h>

#include <commctrl.h>
#include <objbase.h>

#include <wil/resource.h>

#include <filesystem>
#include <format>
#include <functional>
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
		return ExitCodeBadArguments;
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

		RefreshRequest request;
		request.settings = LoadUserSettings(settingsFile, error);
		if (!error.empty())
		{
			report(L"user.json: " + error + L" (using defaults)");
		}
		request.root = PathResolver::ResolveRoot(options.root, request.settings.root, exeDirectory);
		request.catalogFile = dataDirectory / L"catalog.json";
		request.selfDirectory = exeDirectory;
		if (options.verbose)
		{
			request.progress = [](std::wstring_view step) { WriteLog(step); };
		}
		if (fs::exists(request.catalogFile, existsError))
		{
			std::wstring catalogError;
			request.previous = LoadCatalog(request.catalogFile, catalogError);
			if (!request.previous)
			{
				report(L"catalog.json: " + catalogError + L" (starting a new catalog)");
			}
		}

		const RefreshOutcome outcome = RefreshCatalog(request);
		if (!outcome.rootFound)
		{
			// Never replace a catalog with an empty one because a drive is unplugged
			report(std::format(L"AppTray: cannot read {} (catalog left as it was)", request.root.wstring()));
			return ExitCodeRootMissing;
		}
		if (!outcome.saved)
		{
			report(outcome.error);
			return ExitCodeScanFailed;
		}
		std::wstring saveError;
		if (!hadSettings && error.empty() && !SaveUserSettings(settingsFile, request.settings, saveError))
		{
			report(saveError);
		}
		if (options.report && !WriteScanReport(fs::absolute(*options.report), outcome.catalog, outcome.scan, saveError))
		{
			report(saveError);
			return ExitCodeScanFailed;
		}

		report(std::format(
			L"AppTray: {} apps in {} ms ({} folders, {} executables inspected) -> {}",
			outcome.catalog.apps.size(),
			outcome.scan.elapsedMilliseconds,
			outcome.scan.foldersVisited,
			outcome.scan.executablesInspected,
			request.catalogFile.wstring()));
		return 0;
	}

	// One tray icon per AppTray folder. A second start forwards --show / --exit to the copy of
	// the same folder (its hidden window is titled with the instance name), waiting a little if
	// that copy is still loading its catalog.
	void ForwardToRunningInstance(const CommandLineOptions& options, const std::wstring& instanceName)
	{
		HWND running = nullptr;
		for (int attempt = 0; attempt < 30 && running == nullptr; ++attempt)
		{
			running = FindWindowW(TrayWindowClass, instanceName.c_str());
			if (running == nullptr)
			{
				Sleep(100);
			}
		}
		if (running == nullptr)
		{
			return;
		}
		DWORD processId = 0;
		GetWindowThreadProcessId(running, &processId);
		AllowSetForegroundWindow(processId); // so the running instance can bring its flyout up
		PostMessageW(running, options.exit ? WM_APP_EXIT : WM_APP_SHOW, 0, 0);
	}
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
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

	const std::wstring instanceName = GetInstanceName(exeDirectory.wstring());
	wil::unique_mutex_nothrow instanceMutex(CreateMutexW(nullptr, FALSE, (L"Local\\" + instanceName).c_str()));
	const DWORD mutexError = GetLastError();
	// ACCESS_DENIED: the running copy is elevated, so its mutex cannot be opened from here
	if (mutexError == ERROR_ALREADY_EXISTS || (!instanceMutex && mutexError == ERROR_ACCESS_DENIED))
	{
		ForwardToRunningInstance(options, instanceName);
		return 0;
	}
	if (options.exit)
	{
		return 0; // nothing to close
	}

	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	INITCOMMONCONTROLSEX controls{};
	controls.dwSize = sizeof(controls);
	controls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES;
	InitCommonControlsEx(&controls);

	int exitCode = 0;
	{
		TrayApp app(instance, {exeDirectory, dataDirectory, options.root, instanceName});
		exitCode = app.Run(options.show);
	}
	if (SUCCEEDED(com))
	{
		CoUninitialize();
	}
	return exitCode;
}
