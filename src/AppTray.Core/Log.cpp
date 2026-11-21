#include "Log.h"

#include "StringUtil.h"

#include <windows.h>

#include <wil/resource.h>

#include <format>
#include <mutex>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		constexpr std::uintmax_t MaxLogBytes = 100 * 1024;

		std::mutex logMutex;
		fs::path logFile;
	}

	void InitializeLog(const fs::path& file)
	{
		std::scoped_lock lock(logMutex);
		logFile = file;
		std::error_code error;
		fs::create_directories(file.parent_path(), error);
	}

	void WriteLog(std::wstring_view message)
	{
		std::scoped_lock lock(logMutex);
		if (logFile.empty())
		{
			return;
		}
		std::error_code error;
		if (fs::exists(logFile, error) && fs::file_size(logFile, error) > MaxLogBytes)
		{
			fs::remove(logFile, error);
		}

		SYSTEMTIME now{};
		GetLocalTime(&now);
		const std::string line = ToUtf8(std::format(
			L"{:04}-{:02}-{:02} {:02}:{:02}:{:02}  {}\r\n",
			now.wYear,
			now.wMonth,
			now.wDay,
			now.wHour,
			now.wMinute,
			now.wSecond,
			message));

		wil::unique_hfile handle(CreateFileW(
			logFile.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (handle)
		{
			DWORD written = 0;
			WriteFile(handle.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
		}
	}
}
