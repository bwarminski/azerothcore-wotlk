// ABOUTME: Validates XP tracking helpers for playerbot gear upgrades.
// ABOUTME: Ensures counters persist per bot, reset on init, and use config chunk values.

#include "Config.h"
#include "AchievementScript.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTestUtils.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <ctime>
#include <limits>

using namespace testing;

class XpTrackingTest_Accessor
{
public:
    static void TriggerBotLogin(Player* bot) { sRandomPlayerbotMgr->OnBotLoginInternal(bot); }
    static void PrimeXpCache(ObjectGuid::LowType guid, uint32 value = 0)
    {
        sRandomPlayerbotMgr->eventCache[guid]["xp_upgrade"] =
            CachedEvent(value, static_cast<uint32>(time(nullptr)),
                std::numeric_limits<uint32>::max(), "");
    }

    static void PrimeBotCountCache()
    {
        sRandomPlayerbotMgr->eventCache[0]["bot_count"] =
            CachedEvent(0, static_cast<uint32>(time(nullptr)),
                std::numeric_limits<uint32>::max(), "");
    }
};

namespace
{

class XpTrackingTest : public ::testing::Test
{
protected:
    static void EnsureScriptRegistriesInitialized()
    {
        static bool initialized = false;
        if (!initialized)
        {
            ScriptRegistry<MiscScript>::InitEnabledHooksIfNeeded(MISCHOOK_END);
            ScriptRegistry<WorldObjectScript>::InitEnabledHooksIfNeeded(WORLDOBJECTHOOK_END);
            ScriptRegistry<UnitScript>::InitEnabledHooksIfNeeded(UNITHOOK_END);
            ScriptRegistry<PlayerScript>::InitEnabledHooksIfNeeded(PLAYERHOOK_END);
            ScriptRegistry<CommandSC>::InitEnabledHooksIfNeeded(ALLCOMMANDHOOK_END);
            ScriptRegistry<AchievementScript>::InitEnabledHooksIfNeeded(ACHIEVEMENTHOOK_END);
            initialized = true;
        }
    }

    class TestPlayer : public Player
    {
    public:
        using Player::Player;

        void ForceInitValues(ObjectGuid::LowType guidLow)
        {
            Object::_Create(guidLow, uint32(0), HighGuid::Player);
        }
    };

    void SetUp() override
    {
        EnsureScriptRegistriesInitialized();

        originalWorld = sWorld.release();
        worldMock = new NiceMock<WorldMock>();
        sWorld.reset(worldMock);

        static std::string emptyString;
        ON_CALL(*worldMock, GetDataPath()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetRealmName()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetDefaultDbcLocale()).WillByDefault(Return(LOCALE_enUS));
        ON_CALL(*worldMock, getRate(_)).WillByDefault(Return(1.0f));
        ON_CALL(*worldMock, getBoolConfig(_)).WillByDefault(Return(false));
        ON_CALL(*worldMock, getIntConfig(_)).WillByDefault(Return(DEFAULT_MAX_LEVEL));

        session = new WorldSession(1, "xp-tracking", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues(primaryBotGuid);
        session->SetPlayer(player);
        player->SetSession(session);
        XpTrackingTest_Accessor::PrimeXpCache(primaryBotGuid);
        XpTrackingTest_Accessor::PrimeBotCountCache();
    }

    void TearDown() override
    {
        sRandomPlayerbotMgr->ResetXpForUpgrade(primaryBotGuid);
        if (secondaryPlayer)
        {
            sRandomPlayerbotMgr->ResetXpForUpgrade(secondaryPlayer->GetGUID().GetCounter());
        }
        if (!configPath.empty())
        {
            PlayerbotTestUtils::RemoveFileIfExists(configPath);
        }

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        worldMock = nullptr;
        session = nullptr;
        player = nullptr;
        secondarySession = nullptr;
        secondaryPlayer = nullptr;
    }

    void PrepareSecondaryBot(ObjectGuid::LowType guid)
    {
        secondarySession = new WorldSession(2, "xp-tracking-secondary", 0, nullptr, SEC_PLAYER,
            EXPANSION_WRATH_OF_THE_LICH_KING, 0, LOCALE_enUS, 0, false, false, 0);
        secondaryPlayer = new TestPlayer(secondarySession);
        secondaryPlayer->ForceInitValues(guid);
        secondarySession->SetPlayer(secondaryPlayer);
        secondaryPlayer->SetSession(secondarySession);
        XpTrackingTest_Accessor::PrimeXpCache(guid);
    }

    void LoadConfig(uint32 chunkValue)
    {
        configPath = PlayerbotTestUtils::CreatePlayerbotConfig({
            {"AiPlayerbot.Enabled", "1"},
            {"AiPlayerbot.SkipInitialSetup", "1"},
            {"AiPlayerbot.XpUpgradeEnabled", "1"},
            {"AiPlayerbot.XpUpgradeChunk", std::to_string(chunkValue)},
        });
        sConfigMgr->Configure(configPath, std::vector<std::string>());
        sConfigMgr->LoadAppConfigs();
        PlayerbotAIConfig::instance()->Initialize();
    }

    std::string configPath;
    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
    WorldSession* secondarySession = nullptr;
    TestPlayer* secondaryPlayer = nullptr;
    static constexpr ObjectGuid::LowType primaryBotGuid = 101;
};

TEST_F(XpTrackingTest, TracksXpByBotGuid)
{
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 0u);

    sRandomPlayerbotMgr->AddXpForUpgrade(primaryBotGuid, 1200);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 1200u);

    PrepareSecondaryBot(202);
    sRandomPlayerbotMgr->AddXpForUpgrade(secondaryPlayer->GetGUID().GetCounter(), 500);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 1200u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(secondaryPlayer->GetGUID().GetCounter()), 500u);

    sRandomPlayerbotMgr->ConsumeXpForUpgrade(primaryBotGuid, 700);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 500u);
}

TEST_F(XpTrackingTest, PreservesXpOnBotInitialization)
{
    sRandomPlayerbotMgr->AddXpForUpgrade(primaryBotGuid, 2000);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 2000u);

    XpTrackingTest_Accessor::TriggerBotLogin(player);

    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), 2000u);
}

TEST_F(XpTrackingTest, ReadsUpgradeChunkFromConfig)
{
    LoadConfig(9000);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpUpgradeChunkSize(), 9000u);
}
} // namespace
