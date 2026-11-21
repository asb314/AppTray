#pragma once

#include <windows.h>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>

namespace apptray
{
	// Icon of an app target: the exe's own icon (read without the shell's icon handlers), or a
	// light substitute for scripts, shortcuts and folders. Never null: falls back to a stock icon.
	HICON ExtractAppIcon(const std::filesystem::path& path, int size);

	// 32-bit premultiplied bitmap of an icon, the format menus need for alpha (WIC)
	HBITMAP CreateMenuBitmap(HICON icon, int size);

	// Result of an icon request, posted to the window that asked (which then owns it)
	struct IconResult
	{
		std::uint64_t generation = 0;
		std::size_t key = 0;
		HICON icon = nullptr;
	};

	// Extracts icons on a background thread so the launcher opens at once and fills its icons in.
	// Destroying the worker never waits: an extraction stuck on a slow or offline path must not
	// freeze the tray. The thread finishes on its own and posts nothing more after that.
	class IconWorker
	{
	public:
		IconWorker(HWND notify, UINT message);
		~IconWorker();
		IconWorker(const IconWorker&) = delete;
		IconWorker& operator=(const IconWorker&) = delete;

		void Request(std::uint64_t generation, std::size_t key, std::filesystem::path path, int size);

		// Drops pending requests (results already posted still arrive)
		void Clear();

	private:
		struct Job
		{
			std::uint64_t generation = 0;
			std::size_t key = 0;
			std::filesystem::path path;
			int size = 0;
		};

		// Shared with the thread, which may outlive the worker
		struct State
		{
			std::mutex mutex;
			std::condition_variable wake;
			std::deque<Job> jobs;
			bool stopped = false;
			HWND notify = nullptr;
			UINT message = 0;
		};

		static void Run(std::shared_ptr<State> state);

		std::shared_ptr<State> state_;
	};
}
