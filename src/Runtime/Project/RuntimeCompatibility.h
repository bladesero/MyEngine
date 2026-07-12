#pragma once
#include "Core/BuildInfo.h"
#include "Project/FormatVersions.h"

#define MYENGINE_STRINGIZE_DETAIL(x) #x
#define MYENGINE_STRINGIZE(x) MYENGINE_STRINGIZE_DETAIL(x)
#ifndef MYENGINE_BUILD_ID
#define MYENGINE_BUILD_ID local
#endif
#ifndef MYENGINE_BUILD_CONFIGURATION
#define MYENGINE_BUILD_CONFIGURATION unknown
#endif

namespace RuntimeCompatibility {
inline constexpr const char* kEngineVersion = BuildInfo::EngineVersion.data();
inline constexpr const char* kBuildId = BuildInfo::BuildId.data();
inline constexpr const char* kConfiguration = BuildInfo::Configuration.data();
inline constexpr int kContentSchemaVersion = 2;
inline constexpr int kArchiveFormatVersion = FormatVersions::ContentArchive;
}
