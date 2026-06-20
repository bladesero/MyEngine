#pragma once

#define MYENGINE_STRINGIZE_DETAIL(x) #x
#define MYENGINE_STRINGIZE(x) MYENGINE_STRINGIZE_DETAIL(x)
#ifndef MYENGINE_BUILD_ID
#define MYENGINE_BUILD_ID local
#endif
#ifndef MYENGINE_BUILD_CONFIGURATION
#define MYENGINE_BUILD_CONFIGURATION unknown
#endif

namespace RuntimeCompatibility {
inline constexpr const char* kEngineVersion = "0.1.0";
inline constexpr const char* kBuildId = MYENGINE_STRINGIZE(MYENGINE_BUILD_ID);
inline constexpr const char* kConfiguration = MYENGINE_STRINGIZE(MYENGINE_BUILD_CONFIGURATION);
inline constexpr int kContentSchemaVersion = 2;
inline constexpr int kArchiveFormatVersion = 2;
}
