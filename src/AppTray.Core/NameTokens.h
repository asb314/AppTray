#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace apptray
{
	// Removes a launchable extension (.exe, .bat, .cmd, .ps1, .jar, .lnk) and a PortableApps ".paf"
	// marker: "GIMPPortable_3.2.paf.exe" -> "GIMPPortable_3.2.paf" -> "GIMPPortable_3.2"
	std::wstring StripLaunchExtension(std::wstring_view fileName);

	// Splits a name into lowercase words at separators and letter/digit boundaries (and camelCase
	// humps when asked). Only digit runs are dropped: "WinSetupFromUSB_1-10" -> win|setup|from|usb
	std::vector<std::wstring> SplitNameWords(std::wstring_view name, bool splitCamelCase);

	// SplitNameWords without single letters and noise words ("portable", "x64", "win64",
	// "setup"...): the words that say which program a name refers to
	std::vector<std::wstring> TokenizeName(std::wstring_view name, bool splitCamelCase);

	// Tokens without camelCase splitting, joined: "picpick_portable" -> "picpick"
	std::wstring NormalizeName(std::wstring_view name);

	// How well a file name (or a product name) matches a folder name, 0..100:
	// 100 same normalized name, 70 one contains the other, 40-60 shared tokens,
	// 30 a long token of one appears inside the other, 0 unrelated.
	int ScoreNameMatch(std::wstring_view folderName, std::wstring_view fileName);

	// Tidies a version resource string down to its number: "1, 2, 3, 0" -> "1.2.3",
	// "Version 6.0a (Build 150)" -> "6.0a", "Release 0.85" -> "0.85", "2026.10.21 on Python 3.10"
	// -> "2026.10.21", "0.1.3+8909da1" -> "0.1.3". Empty without a leading number ("BadCopy Pro")
	// or when it is all zeros.
	std::wstring NormalizeVersion(std::wstring_view raw);

	// First dotted number in a folder or file name: "QGIS 4.0.0 light" -> "4.0.0",
	// "rufus-4.15p" -> "4.15"; empty when there is none ("HandBrake_1", "7-Zip")
	std::wstring FindVersionInName(std::wstring_view name);
}
