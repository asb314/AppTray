#include "PathResolver.h"

#include "StringUtil.h"

#include <algorithm>

namespace fs = std::filesystem;

namespace apptray
{
	namespace
	{
		fs::path MakeCanonical(const fs::path& path)
		{
			std::error_code error;
			fs::path absolute = fs::absolute(path, error);
			if (error)
			{
				absolute = path;
			}
			return absolute.lexically_normal();
		}

		std::wstring TrimTrailingSeparators(std::wstring text)
		{
			while (text.size() > 3 && (text.back() == L'\\' || text.back() == L'/'))
			{
				text.pop_back();
			}
			return text;
		}
	}

	PathResolver::PathResolver(fs::path root)
		: root_(fs::path(TrimTrailingSeparators(MakeCanonical(root).wstring())))
	{
	}

	std::optional<std::wstring> PathResolver::ToRelative(const fs::path& absolute) const
	{
		const std::wstring rootText = root_.wstring();
		const std::wstring pathText = TrimTrailingSeparators(MakeCanonical(absolute).wstring());
		if (EqualsIgnoreCase(pathText, rootText))
		{
			return std::wstring{};
		}
		if (!StartsWithIgnoreCase(pathText, rootText))
		{
			return std::nullopt;
		}
		// The root must be followed by a separator: "C:\Apps" is not inside "C:\App"
		size_t start = rootText.size();
		if (rootText.back() != L'\\')
		{
			if (pathText[start] != L'\\')
			{
				return std::nullopt;
			}
			++start;
		}
		std::wstring relative = pathText.substr(start);
		std::replace(relative.begin(), relative.end(), L'\\', L'/');
		return relative;
	}

	std::optional<fs::path> PathResolver::ToAbsolute(std::wstring_view relative) const
	{
		std::wstring text(relative);
		std::replace(text.begin(), text.end(), L'/', L'\\');
		const fs::path candidate = text.empty() ? root_ : (root_ / text).lexically_normal();
		if (!ToRelative(candidate))
		{
			return std::nullopt;
		}
		return candidate;
	}

	fs::path PathResolver::ResolveRoot(
		const std::optional<std::wstring>& fromCommandLine,
		const std::optional<std::wstring>& fromSettings,
		const fs::path& exeDirectory)
	{
		const auto resolve = [&](const std::wstring& value)
		{
			const fs::path path(value);
			return MakeCanonical(path.is_absolute() ? path : exeDirectory / path);
		};
		if (fromCommandLine && !fromCommandLine->empty())
		{
			return resolve(*fromCommandLine);
		}
		if (fromSettings && !fromSettings->empty())
		{
			return resolve(*fromSettings);
		}
		return MakeCanonical(exeDirectory.parent_path());
	}

	std::wstring NormalizeRelative(std::wstring_view relative)
	{
		std::wstring text(relative);
		std::replace(text.begin(), text.end(), L'\\', L'/');
		const size_t first = text.find_first_not_of(L'/');
		if (first == std::wstring::npos)
		{
			return {};
		}
		const size_t last = text.find_last_not_of(L'/');
		return text.substr(first, last - first + 1);
	}

	bool IsSameRelative(std::wstring_view a, std::wstring_view b)
	{
		return EqualsIgnoreCase(NormalizeRelative(a), NormalizeRelative(b));
	}

	std::wstring GetParentOfRelative(std::wstring_view relative)
	{
		const size_t slash = relative.rfind(L'/');
		return slash == std::wstring_view::npos ? std::wstring{} : std::wstring(relative.substr(0, slash));
	}

	std::wstring GetLeafOfRelative(std::wstring_view relative)
	{
		const size_t slash = relative.rfind(L'/');
		return std::wstring(slash == std::wstring_view::npos ? relative : relative.substr(slash + 1));
	}
}
