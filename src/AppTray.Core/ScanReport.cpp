#include "ScanReport.h"

#include "PathResolver.h"
#include "StringUtil.h"

#include <windows.h>

#include <wil/resource.h>

#include <format>
#include <map>

namespace apptray
{
	namespace
	{
		constexpr int LowConfidence = 40;

		// Markdown table cells cannot hold '|' or line breaks
		std::wstring EscapeCell(std::wstring_view text)
		{
			std::wstring result;
			for (const wchar_t c : text)
			{
				if (c == L'|')
				{
					result += L"\\|";
				}
				else if (c == L'\r' || c == L'\n')
				{
					result += L' ';
				}
				else
				{
					result += c;
				}
			}
			return result;
		}

		// Target shown relative to the app's own folder: "bin/qgis-bin.exe"
		std::wstring FormatShortTarget(const AppEntry& entry)
		{
			if (entry.target.empty())
			{
				return L"(opens the folder)";
			}
			if (entry.target == entry.id)
			{
				return GetLeafOfRelative(entry.target);
			}
			const std::wstring prefix = entry.id + L"/";
			return entry.target.starts_with(prefix) ? entry.target.substr(prefix.size()) : entry.target;
		}

		std::wstring FormatFlags(const AppEntry& entry)
		{
			std::wstring flags;
			const auto add = [&](std::wstring_view flag)
			{
				if (!flags.empty())
				{
					flags += L", ";
				}
				flags += flag;
			};
			if (entry.requiresAdmin)
			{
				add(L"admin");
			}
			if (entry.console)
			{
				add(L"console");
			}
			if (entry.alternate)
			{
				add(L"copy");
			}
			if (entry.missing)
			{
				add(L"missing");
			}
			if (!entry.readme.empty())
			{
				add(L"readme");
			}
			return flags;
		}

		std::wstring GetTopCategory(const std::wstring& category)
		{
			const size_t slash = category.find(L'/');
			return category.empty() ? L"(root)" : category.substr(0, slash);
		}
	}

	std::string BuildScanReport(const Catalog& catalog, const ScanResult& scan)
	{
		std::wstring out;
		out += L"# AppTray scan report\n\n";
		out += std::format(L"- Root: `{}`\n", catalog.root);
		out += std::format(L"- Scanned at: {}\n", catalog.scannedAt);
		out += std::format(
			L"- Folders visited: {} · executables inspected: {} · time: {} ms\n\n",
			scan.foldersVisited,
			scan.executablesInspected,
			scan.elapsedMilliseconds);

		std::map<std::wstring, int> byKind;
		std::map<std::wstring, std::pair<int, int>> byCategory; // visible, hidden by default
		for (const AppEntry& entry : catalog.apps)
		{
			++byKind[std::wstring(ToString(entry.kind))];
			const bool hiddenByDefault = entry.kind == AppKind::Installer || entry.alternate;
			auto& counts = byCategory[GetTopCategory(entry.category)];
			(hiddenByDefault ? counts.second : counts.first) += 1;
		}

		out += L"## Summary\n\n| Kind | Count |\n|---|---:|\n";
		for (const auto& [kind, count] : byKind)
		{
			out += std::format(L"| {} | {} |\n", kind, count);
		}
		out += std::format(L"| **total** | **{}** |\n\n", catalog.apps.size());

		out += L"## By top-level category\n\n| Category | Visible | Installers and copies |\n|---|---:|---:|\n";
		for (const auto& [category, counts] : byCategory)
		{
			out += std::format(L"| {} | {} | {} |\n", EscapeCell(category), counts.first, counts.second);
		}
		out += L"\n";

		out += std::format(
			L"## Needs review (folder apps whose executable matches the folder name below {})\n\n",
			LowConfidence);
		out += L"| App | Chosen executable | Match | Alternatives |\n|---|---|---:|---:|\n";
		for (const AppEntry& entry : catalog.apps)
		{
			if ((entry.kind == AppKind::Folder || entry.kind == AppKind::Suite) && entry.confidence < LowConfidence)
			{
				out += std::format(
					L"| {} | {} | {} | {} |\n",
					EscapeCell(entry.id),
					EscapeCell(FormatShortTarget(entry)),
					entry.confidence,
					entry.alternatives.size());
			}
		}
		out += L"\n";

		out += L"## Apps\n";
		std::wstring currentCategory = L"\x1";
		for (const AppEntry& entry : catalog.apps)
		{
			if (entry.kind == AppKind::Installer)
			{
				continue;
			}
			if (entry.category != currentCategory)
			{
				currentCategory = entry.category;
				out += std::format(
					L"\n### {}\n\n| Name | Kind | Executable | Match | Flags | Alt. | Description |\n"
					L"|---|---|---|---:|---|---:|---|\n",
					currentCategory.empty() ? L"(root)" : currentCategory);
			}
			out += std::format(
				L"| {} | {} | {} | {} | {} | {} | {} |\n",
				EscapeCell(entry.name),
				ToString(entry.kind),
				EscapeCell(FormatShortTarget(entry)),
				entry.confidence,
				FormatFlags(entry),
				entry.alternatives.size(),
				EscapeCell(entry.description));
		}

		out += L"\n## Installers (hidden by default)\n\n| Id | Executable |\n|---|---|\n";
		for (const AppEntry& entry : catalog.apps)
		{
			if (entry.kind == AppKind::Installer)
			{
				out += std::format(L"| {} | {} |\n", EscapeCell(entry.id), EscapeCell(FormatShortTarget(entry)));
			}
		}

		out += L"\n## Skipped folders\n\n";
		for (const std::wstring& folder : scan.skippedFolders)
		{
			out += std::format(L"- `{}`\n", folder);
		}
		return ToUtf8(out);
	}

	bool WriteScanReport(
		const std::filesystem::path& file, const Catalog& catalog, const ScanResult& scan, std::wstring& error)
	{
		const std::string text = BuildScanReport(catalog, scan);
		wil::unique_hfile handle(CreateFileW(
			file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		DWORD written = 0;
		if (!handle || !WriteFile(handle.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr))
		{
			error = std::format(L"Cannot write {} (error {})", file.wstring(), GetLastError());
			return false;
		}
		return true;
	}
}
