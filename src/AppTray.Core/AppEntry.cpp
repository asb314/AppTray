#include "AppEntry.h"

namespace apptray
{
	std::wstring_view ToString(AppKind kind)
	{
		switch (kind)
		{
		case AppKind::Folder:
			return L"folder";
		case AppKind::File:
			return L"file";
		case AppKind::PortableApps:
			return L"portableApps";
		case AppKind::Installer:
			return L"installer";
		case AppKind::Suite:
			return L"suite";
		}
		return L"folder";
	}

	std::optional<AppKind> ParseAppKind(std::wstring_view text)
	{
		for (const AppKind kind :
			 {AppKind::Folder, AppKind::File, AppKind::PortableApps, AppKind::Installer, AppKind::Suite})
		{
			if (ToString(kind) == text)
			{
				return kind;
			}
		}
		return std::nullopt;
	}
}
