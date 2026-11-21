#pragma once

// Single source of the version: used by the code (update check) and by AppTray.rc. Plain macros,
// because the resource compiler cannot read C++ constants. Bump these before a release.
#define APPTRAY_VERSION_MAJOR 0
#define APPTRAY_VERSION_MINOR 3
#define APPTRAY_VERSION_PATCH 0
#define APPTRAY_VERSION_TEXT "0.3.0"
#define APPTRAY_VERSION_WIDE L"0.3.0"

// Where releases are published (GitHub "owner/repository"); forks change it here
#define APPTRAY_REPOSITORY L"asb314/AppTray"
