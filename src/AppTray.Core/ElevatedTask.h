#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace apptray
{
	// Scheduled tasks, one per app, that start the app elevated without a UAC prompt (the same
	// trick as simplewall's own "skip UAC" task). Creating or deleting one needs an elevated
	// process; running one does not. They live in the "\AppTray" folder of the Task Scheduler.
	// COM must be initialized on the calling thread.
	//
	// Note: the task runs whatever executable sits at that path, and the apps folder is writable
	// by the user, so anything able to replace that exe gets it run elevated. Same trade-off as
	// any "skip UAC" task; use it only for apps that need it.

	// "Network/Diagnostics/simplewall" -> "Network - Diagnostics - simplewall (1a2b3c4d)": readable,
	// plus a hash of the whole id so that ids differing only in punctuation never share a task
	std::wstring MakeElevatedTaskName(std::wstring_view appId);

	// DOMAIN\user of this process
	std::wstring GetCurrentUserName();

	// user: the account the task runs for. The elevated copy that registers the task may run as
	// another account (a standard user typing an administrator's password), so the caller passes
	// the user of the non-elevated AppTray. It elevates only for accounts in Administrators.
	bool RegisterElevatedTask(
		const std::wstring& name, const std::filesystem::path& program, const std::wstring& arguments,
		const std::wstring& user, std::wstring& error);
	bool DeleteElevatedTask(const std::wstring& name, std::wstring& error);

	enum class ElevatedTaskRun
	{
		Started,
		Stale,  // the task opens another executable or other arguments than the app does now
		Failed, // missing, or the Task Scheduler refused
	};
	// Starts the task only if it still opens program with arguments
	ElevatedTaskRun RunElevatedTask(
		const std::wstring& name, const std::filesystem::path& program, const std::wstring& arguments,
		std::wstring& error);
}
