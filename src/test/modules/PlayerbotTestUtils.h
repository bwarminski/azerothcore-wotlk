// ABOUTME: Provides shared helpers for playerbot module tests.
// ABOUTME: Supports temporary config creation and cleanup utilities.

#pragma once

#include <boost/filesystem.hpp>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>

namespace PlayerbotTestUtils
{
inline std::string CreateConfig(std::string const& section, std::map<std::string, std::string> const& values)
{
    auto path = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("playerbots-config-%%%%-%%%%.conf");
    std::ofstream stream(path.c_str());
    stream << "[" << section << "]\n";

    for (auto const& entry : values)
    {
        stream << entry.first << " = " << entry.second << "\n";
    }

    stream.close();
#if WIN32
    auto native = path.native();
    return std::string(native.begin(), native.end());
#else
    return path.native();
#endif
}

inline std::string CreatePlayerbotConfig(std::map<std::string, std::string> const& values)
{
    return CreateConfig("playerbots", values);
}

inline void RemoveFileIfExists(std::string const& path)
{
    if (!path.empty())
    {
        std::remove(path.c_str());
    }
}
} // namespace PlayerbotTestUtils
