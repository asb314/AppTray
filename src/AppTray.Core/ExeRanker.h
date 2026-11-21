#pragma once

#include "ExeInspector.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	enum class LaunchType
	{
		None,
		Exe,
		Script,   // .bat / .cmd
		Jar,
		Shortcut, // .lnk
	};

	LaunchType GetLaunchType(std::wstring_view fileName);

	inline bool IsLaunchable(std::wstring_view fileName)
	{
		return GetLaunchType(fileName) != LaunchType::None;
	}

	// setup.exe, unins000.exe, *-update.exe, keygen.exe, GIMPPortable_3.2.paf.exe...
	bool IsInstallerName(std::wstring_view fileName);

	// An executable that could be the main program of an app folder
	struct Candidate
	{
		std::filesystem::path path; // absolute
		std::wstring fileName;
		std::wstring relativeDir;   // folder relative to the app folder ("" or "bin/x64"), '/'
		int depth = 0;              // 0 = directly in the app folder
		ExeInfo info;               // only filled for .exe files
		int score = 0;              // set by RankCandidates
		int match = 0;              // name match part of the score, set by RankCandidates
	};

	// Name match of a candidate with the app folder, 0..100 (file name, product name, description)
	int ScoreFolderMatch(std::wstring_view folderName, const Candidate& candidate);

	// Full score: name match plus type, subsystem, helper words, architecture, depth and size
	int ScoreCandidate(std::wstring_view folderName, const Candidate& candidate);

	// Scores every candidate and sorts them, best first (ties keep a stable, name-based order)
	void RankCandidates(std::wstring_view folderName, std::vector<Candidate>& candidates);
}
