// ABOUTME: Exercises xp gain and level-up hooks for playerbot gear upgrades.
// ABOUTME: Verifies upgrade triggers and xp counter behavior against config settings.

#include "Config.h"
#include "AchievementScript.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTestUtils.h"
#include "Player.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <ctime>
#include <limits>
#include <string>

using namespace testing;

void AddPlayerbotsScripts();

class XpHookTest_Accessor
{
public:
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

    static void AddCurrentBot(ObjectGuid::LowType guid)
    {
        if (std::find(sRandomPlayerbotMgr->currentBots.begin(),
            sRandomPlayerbotMgr->currentBots.end(), guid) == sRandomPlayerbotMgr->currentBots.end())
        {
            sRandomPlayerbotMgr->currentBots.push_back(guid);
        }
    }

    static void RemoveCurrentBot(ObjectGuid::LowType guid)
    {
        sRandomPlayerbotMgr->currentBots.remove(guid);
    }
};

namespace
{
class XpHookTest : public ::testing::Test
{
protected:

    class TestPlayer : public Player
    {
    public:
        using Player::Player;

        void ForceInitValues(ObjectGuid::LowType guidLow)
        {
            Object::_Create(guidLow, uint32(0), HighGuid::Player);
        }
    };

    static ObjectGuid::LowType NextGuid()
    {
        static ObjectGuid::LowType nextGuid = 9001;
        return nextGuid++;
    }

    static void EnsureScriptRegistriesInitialized()
    {
        static bool initialized = false;
        if (!initialized)
        {
            AddPlayerbotsScripts();
            ScriptRegistry<MiscScript>::InitEnabledHooksIfNeeded(MISCHOOK_END);
            ScriptRegistry<WorldObjectScript>::InitEnabledHooksIfNeeded(WORLDOBJECTHOOK_END);
            ScriptRegistry<UnitScript>::InitEnabledHooksIfNeeded(UNITHOOK_END);
            ScriptRegistry<PlayerScript>::InitEnabledHooksIfNeeded(PLAYERHOOK_END);
            ScriptRegistry<CommandSC>::InitEnabledHooksIfNeeded(ALLCOMMANDHOOK_END);
            ScriptRegistry<AchievementScript>::InitEnabledHooksIfNeeded(ACHIEVEMENTHOOK_END);
            initialized = true;
        }
    }

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

        botGuid = NextGuid();
        session = new WorldSession(botGuid, "xp-hook", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0, true);

        player = new TestPlayer(session);
        player->ForceInitValues(botGuid);
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetLevel(10);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 0, RACE_HUMAN);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, CLASS_WARRIOR);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 2, GENDER_MALE);

        XpHookTest_Accessor::PrimeXpCache(botGuid);
        XpHookTest_Accessor::PrimeBotCountCache();
    }

    void TearDown() override
    {
        sRandomPlayerbotMgr->ResetXpForUpgrade(botGuid);
        UnregisterRandomBot();
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
    }

    void LoadConfig(bool enabled, uint32 chunkValue, float xpRate = 1.0f)
    {
        configPath = PlayerbotTestUtils::CreatePlayerbotConfig({
            {"AiPlayerbot.Enabled", "1"},
            {"AiPlayerbot.SkipInitialSetup", "1"},
            {"AiPlayerbot.XpUpgradeEnabled", enabled ? "1" : "0"},
            {"AiPlayerbot.XpUpgradeChunk", std::to_string(chunkValue)},
            {"AiPlayerbot.RandomBotXPRate", std::to_string(xpRate)},
        });
        sConfigMgr->Configure(configPath, std::vector<std::string>());
        sConfigMgr->LoadAppConfigs();
        PlayerbotAIConfig::instance()->Initialize();
        RegisterRandomBot();
    }

    void RegisterRandomBot()
    {
        accountId = session->GetAccountId();
        botName = "xp_hook_bot_" + std::to_string(botGuid);
        sCharacterCache->AddCharacterCacheEntry(player->GetGUID(), accountId, botName, player->getGender(),
            player->getRace(), player->getClass(), player->GetLevel());

        if (std::find(sPlayerbotAIConfig->randomBotAccounts.begin(),
            sPlayerbotAIConfig->randomBotAccounts.end(), accountId) == sPlayerbotAIConfig->randomBotAccounts.end())
        {
            sPlayerbotAIConfig->randomBotAccounts.push_back(accountId);
        }

        XpHookTest_Accessor::AddCurrentBot(botGuid);

        randomBotRegistered = true;
    }

    void UnregisterRandomBot()
    {
        if (!randomBotRegistered)
            return;

        XpHookTest_Accessor::RemoveCurrentBot(botGuid);
        sPlayerbotAIConfig->randomBotAccounts.erase(
            std::remove(sPlayerbotAIConfig->randomBotAccounts.begin(),
                sPlayerbotAIConfig->randomBotAccounts.end(), accountId),
            sPlayerbotAIConfig->randomBotAccounts.end());
        sCharacterCache->DeleteCharacterCacheEntry(player->GetGUID(), botName);
        randomBotRegistered = false;
    }

    std::string configPath;
    ObjectGuid::LowType botGuid = 0;
    uint32 accountId = 0;
    std::string botName;
    bool randomBotRegistered = false;
    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
};

TEST_F(XpHookTest, TriggersUpgradeAndConsumesChunkOnXpGain)
{
    LoadConfig(true, 1000);

    uint32 amount = 400;
    sScriptMgr->OnPlayerGiveXP(player, amount, nullptr, PlayerXPSource::XPSOURCE_QUEST);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(botGuid), 400u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 0u);

    amount = 700;
    sScriptMgr->OnPlayerGiveXP(player, amount, nullptr, PlayerXPSource::XPSOURCE_QUEST);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 1u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(botGuid), 100u);
}

TEST_F(XpHookTest, UsesAdjustedXpAfterRandomBotRate)
{
    LoadConfig(true, 1000, 0.5f);

    uint32 amount = 1000;
    sScriptMgr->OnPlayerGiveXP(player, amount, nullptr, PlayerXPSource::XPSOURCE_QUEST);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 0u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(botGuid), 500u);
}

TEST_F(XpHookTest, TriggersUpgradeOnLevelChange)
{
    LoadConfig(true, 1000);

    sScriptMgr->OnPlayerLevelChanged(player, 9);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 1u);
}

TEST_F(XpHookTest, SkipsUpgradesWhenDisabled)
{
    LoadConfig(false, 1000);

    uint32 amount = 1200;
    sScriptMgr->OnPlayerGiveXP(player, amount, nullptr, PlayerXPSource::XPSOURCE_QUEST);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(botGuid), 0u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 0u);

    sScriptMgr->OnPlayerLevelChanged(player, 9);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 0u);
}
} // namespace
