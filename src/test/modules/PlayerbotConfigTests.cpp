// ABOUTME: Exercises playerbot XP upgrade configuration defaults and overrides.
// ABOUTME: Confirms PlayerbotAIConfig exposes new upgrade settings through getters.

#include "PlayerbotAIConfig.h"
#include "Config.h"
#include "IndividualProgression.h"
#include "gtest/gtest.h"
#include "World.h"
#include "WorldMock.h"

#include <boost/filesystem.hpp>
#include <fstream>
#include <map>

using namespace testing;

namespace
{
std::string CreatePlayerbotConfig(std::map<std::string, std::string> const& values)
{
    auto path = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("playerbots-config-%%%%-%%%%.conf");
    std::ofstream stream(path.c_str());
    stream << "[playerbots]\n";

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

class PlayerbotConfigTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        if (!configPath.empty())
        {
            std::remove(configPath.c_str());
        }
        if (originalWorld)
        {
            IWorld* currentWorld = sWorld.release();
            delete currentWorld;
            sWorld.reset(originalWorld);
            originalWorld = nullptr;
            worldMock = nullptr;
        }
    }

    void LoadConfig(std::map<std::string, std::string> const& values)
    {
        configPath = CreatePlayerbotConfig(values);
        sConfigMgr->Configure(configPath, std::vector<std::string>());
        sConfigMgr->LoadAppConfigs();
        originalWorld = sWorld.release();
        worldMock = new NiceMock<WorldMock>();
        sWorld.reset(worldMock);
        ASSERT_TRUE(dynamic_cast<WorldMock*>(sWorld.get()) != nullptr);
        ON_CALL(*worldMock, getIntConfig(_)).WillByDefault(Return(DEFAULT_MAX_LEVEL));
        PlayerbotAIConfig::instance()->Initialize();
    }

    std::string configPath;
    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
};

TEST_F(PlayerbotConfigTest, DefaultsMatchExpectedValues)
{
    LoadConfig({
        {"AiPlayerbot.Enabled", "1"},
        {"AiPlayerbot.SkipInitialSetup", "1"},
    });

    PlayerbotAIConfig* config = PlayerbotAIConfig::instance();
    EXPECT_FALSE(config->IsXpUpgradeEnabled());
    EXPECT_EQ(config->GetXpUpgradeChunk(), 7000u);
    EXPECT_TRUE(config->IsVendorSeedEnabled());
    EXPECT_EQ(config->GetProgressionState(), static_cast<uint8>(PROGRESSION_START));
    EXPECT_EQ(config->GetBiSWeeksAtEndgame(), 0u);
}

TEST_F(PlayerbotConfigTest, ReadsOverridesFromConfig)
{
    LoadConfig({
        {"AiPlayerbot.Enabled", "1"},
        {"AiPlayerbot.XpUpgradeEnabled", "1"},
        {"AiPlayerbot.XpUpgradeChunk", "9000"},
        {"AiPlayerbot.VendorSeedEnabled", "0"},
        {"AiPlayerbot.ProgressionState", std::to_string(PROGRESSION_AQ)},
        {"AiPlayerbot.BiSWeeksAtEndgame", "6"},
        {"AiPlayerbot.SkipInitialSetup", "1"},
    });

    PlayerbotAIConfig* config = PlayerbotAIConfig::instance();
    EXPECT_TRUE(config->IsXpUpgradeEnabled());
    EXPECT_EQ(config->GetXpUpgradeChunk(), 9000u);
    EXPECT_FALSE(config->IsVendorSeedEnabled());
    EXPECT_EQ(config->GetProgressionState(), static_cast<uint8>(PROGRESSION_AQ));
    EXPECT_EQ(config->GetBiSWeeksAtEndgame(), 6u);
}
} // namespace
