#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace apptray
{
	// Converts between absolute paths and paths relative to the scan root. Relative paths use '/'
	// so the catalog reads the same on any drive letter and the whole tree can be moved.
	class PathResolver
	{
	public:
		explicit PathResolver(std::filesystem::path root);

		const std::filesystem::path& Root() const
		{
			return root_;
		}

		// "C:\Applications\Media\Tools" -> "Media/Tools"; root itself -> ""; outside -> nullopt
		std::optional<std::wstring> ToRelative(const std::filesystem::path& absolute) const;

		// "Media/Tools" -> "C:\Applications\Media\Tools"; nullopt if it would escape the root
		std::optional<std::filesystem::path> ToAbsolute(std::wstring_view relative) const;

		// Root from (in order) the command line, user.json, or the parent of AppTray's folder.
		// Relative values are taken from AppTray's folder.
		static std::filesystem::path ResolveRoot(
			const std::optional<std::wstring>& fromCommandLine,
			const std::optional<std::wstring>& fromSettings,
			const std::filesystem::path& exeDirectory);

	private:
		std::filesystem::path root_;
	};

	// Parent part of a relative path: "Media/Tools/ffmpeg" -> "Media/Tools", "ffmpeg" -> ""
	std::wstring GetParentOfRelative(std::wstring_view relative);

	// Last part of a relative path: "Media/Tools/ffmpeg" -> "ffmpeg"
	std::wstring GetLeafOfRelative(std::wstring_view relative);
}
