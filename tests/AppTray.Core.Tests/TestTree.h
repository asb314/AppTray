#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

namespace apptray::tests
{
	// A throw-away folder tree under %TEMP%\AppTrayTests, deleted when the test ends
	class TestTree
	{
	public:
		TestTree()
		{
			static std::atomic<int> counter{0};
			root_ = std::filesystem::temp_directory_path() / L"AppTrayTests"
				/ std::format(L"{}-{}-{}", GetCurrentProcessId(), GetTickCount64(), counter++);
			std::filesystem::create_directories(root_);
		}

		~TestTree()
		{
			std::error_code error;
			std::filesystem::remove_all(root_, error);
		}

		TestTree(const TestTree&) = delete;
		TestTree& operator=(const TestTree&) = delete;

		const std::filesystem::path& Root() const
		{
			return root_;
		}

		// Creates the file (and its folders); '/' separators allowed
		std::filesystem::path Add(std::wstring_view relative, std::string_view content = {})
		{
			std::filesystem::path path = root_ / std::filesystem::path(relative).make_preferred();
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::binary);
			file.write(content.data(), static_cast<std::streamsize>(content.size()));
			return path;
		}

	private:
		std::filesystem::path root_;
	};
}
