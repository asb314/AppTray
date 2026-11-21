#include "ExeRanker.h"

#include "NameTokens.h"
#include "StringUtil.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace apptray
{
	namespace
	{
		// Whole words of a file name that point to an installer or a crack, not the program:
		// "Firebird-2.5-Setup", "unins000", "PATCH FOR SLYSOFT", "Keygen"
		constexpr std::wstring_view InstallerWords[] = {
			L"setup", L"install", L"installer", L"uninstall", L"uninst", L"unins", L"update",
			L"updater", L"patch", L"keygen", L"crack", L"redist", L"vcredist", L"activator",
			L"keymaker", L"kg",
		};

		// Beginnings or endings of a single glued word: "innosetup", "SetupAnyDVD",
		// "odbcibinstall". Not anywhere inside: "WinSetupFromUSB" is a program.
		constexpr std::wstring_view InstallerAffixes[] = {
			L"setup", L"install", L"installer", L"uninstall", L"unins",
		};

		// Endings only: "harddisksentinelupdate"
		constexpr std::wstring_view InstallerSuffixes[] = {L"update", L"updater"};

		// Tokens of helper executables that ship next to the real program
		constexpr std::wstring_view HelperWords[] = {
			L"plugin", L"plugins", L"uploader", L"helper", L"service", L"svc", L"cli",
			L"console", L"debug", L"proxy", L"cache", L"gen", L"dispatcher", L"senddmp",
			L"dmp", L"crash", L"crashhandler", L"reporter", L"report", L"agent", L"daemon",
			L"tray", L"server", L"test", L"sample", L"demo", L"register", L"elevate",
			L"watchdog", L"python", L"pythonw", L"java", L"javaw", L"node", L"perl",
			L"shell", L"ctrl",
		};

		// Whole words that mark a script as the intended way to start a portable app
		constexpr std::wstring_view LaunchScriptWords[] = {L"portable", L"launch", L"launcher", L"start", L"run"};

		// "Shotcut portable" yes, "blender_factory_startup" no: whole words only
		bool IsLaunchScriptName(std::wstring_view lowerStem)
		{
			size_t start = 0;
			while (start < lowerStem.size())
			{
				size_t end = start;
				while (end < lowerStem.size() && IsCharAlphaNumericW(lowerStem[end]))
				{
					++end;
				}
				const std::wstring_view word = lowerStem.substr(start, end - start);
				if (!word.empty()
					&& std::find(std::begin(LaunchScriptWords), std::end(LaunchScriptWords), word)
						!= std::end(LaunchScriptWords))
				{
					return true;
				}
				start = end + 1;
			}
			return false;
		}

		bool ContainsAny(std::wstring_view lowerText, std::initializer_list<std::wstring_view> words)
		{
			return std::any_of(words.begin(), words.end(), [&](std::wstring_view w)
				{ return lowerText.find(w) != std::wstring_view::npos; });
		}

		template <size_t N>
		bool IsOneOf(std::wstring_view token, const std::wstring_view (&words)[N])
		{
			return std::find(std::begin(words), std::end(words), token) != std::end(words);
		}

		int ScoreArchitecture(const Candidate& candidate)
		{
			switch (candidate.info.machine)
			{
			case IMAGE_FILE_MACHINE_AMD64:
				return 10;
			case IMAGE_FILE_MACHINE_ARM64:
				return -40;
			case IMAGE_FILE_MACHINE_I386:
				return 0;
			default:
				break;
			}
			// Not a PE (or not inspected): guess from the folders it lives in
			const std::wstring dir = ToLower(candidate.relativeDir);
			if (ContainsAny(dir, {L"arm64", L"arm"}))
			{
				return -40;
			}
			if (ContainsAny(dir, {L"x64", L"amd64", L"64-bit", L"64bit", L"win64"}))
			{
				return 10;
			}
			return 0;
		}

		int ScoreSize(std::uint64_t size)
		{
			if (size <= 65536)
			{
				return 0;
			}
			const int steps = static_cast<int>(std::bit_width(size / 65536)) - 1;
			return std::clamp(steps, 0, 6);
		}
	}

	LaunchType GetLaunchType(std::wstring_view fileName)
	{
		if (EndsWithIgnoreCase(fileName, L".exe"))
		{
			return LaunchType::Exe;
		}
		if (EndsWithIgnoreCase(fileName, L".bat") || EndsWithIgnoreCase(fileName, L".cmd"))
		{
			return LaunchType::Script;
		}
		if (EndsWithIgnoreCase(fileName, L".jar"))
		{
			return LaunchType::Jar;
		}
		if (EndsWithIgnoreCase(fileName, L".lnk"))
		{
			return LaunchType::Shortcut;
		}
		return LaunchType::None;
	}

	bool IsInstallerName(std::wstring_view fileName)
	{
		std::wstring_view stem = fileName;
		const size_t dot = stem.rfind(L'.');
		if (dot != std::wstring_view::npos)
		{
			stem = stem.substr(0, dot);
		}
		if (EndsWithIgnoreCase(stem, L".paf"))
		{
			return true;
		}
		for (const std::wstring& word : SplitNameWords(stem, false))
		{
			if (IsOneOf(word, InstallerWords))
			{
				return true;
			}
			for (const std::wstring_view affix : InstallerAffixes)
			{
				if (word.starts_with(affix) || word.ends_with(affix))
				{
					return true;
				}
			}
			for (const std::wstring_view suffix : InstallerSuffixes)
			{
				if (word.ends_with(suffix))
				{
					return true;
				}
			}
		}
		// camelCase: "FiddlerSetup", "SetupVirtualCloneDrive" (first or last hump only)
		const std::vector<std::wstring> humps = SplitNameWords(stem, true);
		return !humps.empty()
			&& (IsOneOf(humps.front(), InstallerAffixes) || IsOneOf(humps.back(), InstallerAffixes));
	}

	int ScoreFolderMatch(std::wstring_view folderName, const Candidate& candidate)
	{
		int match = ScoreNameMatch(folderName, candidate.fileName);
		if (!candidate.info.productName.empty())
		{
			match = std::max(match, ScoreNameMatch(folderName, candidate.info.productName) * 8 / 10);
		}
		if (!candidate.info.fileDescription.empty())
		{
			match =
				std::max(match, ScoreNameMatch(folderName, candidate.info.fileDescription) * 6 / 10);
		}
		return match;
	}

	int ScoreCandidate(std::wstring_view folderName, const Candidate& candidate)
	{
		int score = ScoreFolderMatch(folderName, candidate);
		const std::wstring stem = StripLaunchExtension(candidate.fileName);
		const std::wstring lowerStem = ToLower(stem);

		switch (GetLaunchType(candidate.fileName))
		{
		case LaunchType::Exe:
			if (candidate.info.isGui)
			{
				score += 20;
			}
			else if (candidate.info.isConsole)
			{
				score -= 10;
			}
			break;
		case LaunchType::Script:
			score += IsLaunchScriptName(lowerStem) ? 60 : -10;
			break;
		case LaunchType::Shortcut:
			score += 10;
			break;
		case LaunchType::Jar:
		case LaunchType::None:
			break;
		}

		// Helper words count only when the folder name does not contain them too
		const std::vector<std::wstring> folderTokens = TokenizeName(folderName, true);
		int helperPenalty = 0;
		for (const std::wstring& token : TokenizeName(stem, true))
		{
			if (IsOneOf(token, HelperWords)
				&& std::find(folderTokens.begin(), folderTokens.end(), token) == folderTokens.end())
			{
				helperPenalty += 30;
			}
		}
		score -= std::min(helperPenalty, 60);

		score += ScoreArchitecture(candidate);
		score -= 10 * candidate.depth;
		if (!candidate.info.productName.empty())
		{
			score += 3;
		}
		score += ScoreSize(candidate.info.size);
		return score;
	}

	void RankCandidates(std::wstring_view folderName, std::vector<Candidate>& candidates)
	{
		for (Candidate& candidate : candidates)
		{
			candidate.match = ScoreFolderMatch(folderName, candidate);
			candidate.score = ScoreCandidate(folderName, candidate);
		}
		std::stable_sort(
			candidates.begin(),
			candidates.end(),
			[](const Candidate& a, const Candidate& b)
			{
				if (a.score != b.score)
				{
					return a.score > b.score;
				}
				if (a.depth != b.depth)
				{
					return a.depth < b.depth;
				}
				return CompareStringOrdinal(
						   a.fileName.c_str(), -1, b.fileName.c_str(), -1, TRUE)
					== CSTR_LESS_THAN;
			});
	}
}
