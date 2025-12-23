// ABOUTME: Validates progression-based item allowances for individual progression helpers.
// ABOUTME: Uses mocked world and condition providers to assert gating on progression quests.

#include "IndividualProgression.h"
#include "Config.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "PlayerbotTestUtils.h"
#include "Player.h"
#include "ProgressionItemRules.h"
#include "ProgressionConditionProvider.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <map>
#include <memory>
#include <vector>

using namespace testing;

namespace
{
class ProgressionHelperTest : public ::testing::Test
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
            initialized = true;
        }
    }

    class TestPlayer : public Player
    {
    public:
        using Player::Player;

        void UpdateObjectVisibility(bool /*forced*/ = true, bool /*fromUpdate*/ = false) override { }

        void ForceInitValues(ObjectGuid::LowType guidLow = 1)
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
        CaptureItemTemplates();

        static std::string emptyString;
        ON_CALL(*worldMock, GetDataPath()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetRealmName()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetDefaultDbcLocale()).WillByDefault(Return(LOCALE_enUS));
        ON_CALL(*worldMock, getRate(_)).WillByDefault(Return(1.0f));
        ON_CALL(*worldMock, getBoolConfig(_)).WillByDefault(Return(false));
        ON_CALL(*worldMock, getIntConfig(_)).WillByDefault(Return(0));
        ON_CALL(*worldMock, getFloatConfig(_)).WillByDefault(Return(0.0f));

        session = new WorldSession(1, "progression", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING,
            0, LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues();
        session->SetPlayer(player);
        player->SetSession(session);

        sIndividualProgression->enabled = true;
        sIndividualProgression->excludeAccounts = false;
        sIndividualProgression->progressionLimit = 0;

        ResetItemTemplates();
        SetProgressionState(PROGRESSION_START);
    }

    void TearDown() override
    {
        if (!configPath.empty())
        {
            PlayerbotTestUtils::RemoveFileIfExists(configPath);
        }

        sIndividualProgression->SetProgressionConditionProvider(nullptr);

        RestoreItemTemplates();

        sIndividualProgression->enabled = originalEnabled;
        sIndividualProgression->excludeAccounts = originalExcludeAccounts;
        sIndividualProgression->progressionLimit = originalProgressionLimit;

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        worldMock = nullptr;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        session = nullptr;
        player = nullptr;
    }

    void ResetItemTemplates()
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        store->clear();

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        fastStore->clear();
    }

    void StoreItemTemplate(uint32 entry, uint32 requiredLevel, uint32 itemLevel)
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        ItemTemplate& proto = (*store)[entry];
        proto = ItemTemplate();
        proto.ItemId = entry;
        proto.Class = ITEM_CLASS_ARMOR;
        proto.SubClass = 0;
        proto.InventoryType = INVTYPE_HEAD;
        proto.RequiredLevel = requiredLevel;
        proto.ItemLevel = itemLevel;

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        if (fastStore->size() <= entry)
        {
            fastStore->resize(entry + 1, nullptr);
        }
        (*fastStore)[entry] = &proto;
    }

    void SetProgressionState(uint8 state)
    {
        player->UpdatePlayerSetting("mod-individual-progression", SETTING_PROGRESSION_STATE, state);
    }

    void LoadConfig(std::map<std::string, std::string> const& values)
    {
        if (!configPath.empty())
        {
            PlayerbotTestUtils::RemoveFileIfExists(configPath);
        }

        configPath = PlayerbotTestUtils::CreateConfig("worldserver", values);
        sConfigMgr->Configure(configPath, std::vector<std::string>());
        sConfigMgr->LoadAppConfigs();
    }

    void CaptureItemTemplates()
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        originalTemplates = *store;

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        originalFastTemplates = *fastStore;

        originalEnabled = sIndividualProgression->enabled;
        originalExcludeAccounts = sIndividualProgression->excludeAccounts;
        originalProgressionLimit = sIndividualProgression->progressionLimit;
    }

    void RestoreItemTemplates()
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        *store = originalTemplates;

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        *fastStore = originalFastTemplates;
    }

    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
    ItemTemplateContainer originalTemplates;
    std::vector<ItemTemplate*> originalFastTemplates;
    bool originalEnabled = false;
    bool originalExcludeAccounts = false;
    int originalProgressionLimit = 0;
    std::string configPath;
};

class TestProgressionConditionProvider : public ProgressionConditionProvider
{
public:
    void RequireQuest(uint32 itemId, uint32 questId)
    {
        requiredQuests[itemId] = questId;
    }

    void SetQuestStatus(uint32 questId, QuestStatus status)
    {
        questStatuses[questId] = status;
    }

    bool IsItemAllowed(Player* /*player*/, uint32 itemId) const override
    {
        auto itr = requiredQuests.find(itemId);
        if (itr == requiredQuests.end())
        {
            return true;
        }

        auto statusItr = questStatuses.find(itr->second);
        if (statusItr == questStatuses.end())
        {
            return false;
        }

        return statusItr->second == QUEST_STATUS_REWARDED;
    }

private:
    std::map<uint32, uint32> requiredQuests;
    std::map<uint32, QuestStatus> questStatuses;
};

uint32 GetProgressionQuestId(ProgressionState state)
{
    return 66000 + static_cast<uint32>(state);
}

TEST_F(ProgressionHelperTest, BlocksItemsAboveCurrentExpansion)
{
    constexpr uint32 VANILLA_ITEM = 10000;
    constexpr uint32 TBC_ITEM = 20000;

    StoreItemTemplate(VANILLA_ITEM, 60, 70);
    StoreItemTemplate(TBC_ITEM, IP_LEVEL_TBC, 164);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(VANILLA_ITEM, GetProgressionQuestId(PROGRESSION_AQ));
    provider->RequireQuest(TBC_ITEM, GetProgressionQuestId(PROGRESSION_PRE_TBC));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_AQ), QUEST_STATUS_REWARDED);
    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_PRE_TBC), QUEST_STATUS_NONE);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, VANILLA_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, TBC_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksLateTbcGearBeforeSunwell)
{
    constexpr uint32 BT_ITEM = 21000;
    constexpr uint32 SUNWELL_ITEM = 22000;

    StoreItemTemplate(BT_ITEM, IP_LEVEL_TBC, 152);
    StoreItemTemplate(SUNWELL_ITEM, IP_LEVEL_TBC, 164);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(BT_ITEM, GetProgressionQuestId(PROGRESSION_TBC_TIER_2));
    provider->RequireQuest(SUNWELL_ITEM, GetProgressionQuestId(PROGRESSION_TBC_TIER_4));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_2), QUEST_STATUS_REWARDED);
    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_4), QUEST_STATUS_NONE);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, SUNWELL_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksIccGearBeforeIccPhase)
{
    constexpr uint32 ULD_ITEM = 23000;
    constexpr uint32 ICC_ITEM = 24000;

    StoreItemTemplate(ULD_ITEM, IP_LEVEL_WOTLK, 226);
    StoreItemTemplate(ICC_ITEM, IP_LEVEL_WOTLK, 264);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(ULD_ITEM, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_1));
    provider->RequireQuest(ICC_ITEM, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_3));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_1), QUEST_STATUS_REWARDED);
    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_3), QUEST_STATUS_NONE);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, ULD_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, ICC_ITEM));
}

TEST_F(ProgressionHelperTest, AllowsWrathItemsAtEndProgression)
{
    constexpr uint32 WOTLK_ITEM = 30000;
    StoreItemTemplate(WOTLK_ITEM, IP_LEVEL_WOTLK, 200);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(WOTLK_ITEM, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_2));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_2), QUEST_STATUS_REWARDED);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, WOTLK_ITEM));
}

TEST_F(ProgressionHelperTest, UsesConditionProviderForProgressionQuests)
{
    constexpr uint32 QUEST_ALLOWED = 66005;
    constexpr uint32 QUEST_BLOCKED = 66006;
    constexpr uint32 ITEM_ALLOWED = 31000;
    constexpr uint32 ITEM_BLOCKED = 31001;

    StoreItemTemplate(ITEM_ALLOWED, IP_LEVEL_WOTLK, 300);
    StoreItemTemplate(ITEM_BLOCKED, IP_LEVEL_WOTLK, 300);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(ITEM_ALLOWED, QUEST_ALLOWED);
    provider->RequireQuest(ITEM_BLOCKED, QUEST_BLOCKED);
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(QUEST_ALLOWED, QUEST_STATUS_REWARDED);
    provider->SetQuestStatus(QUEST_BLOCKED, QUEST_STATUS_NONE);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, ITEM_ALLOWED));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, ITEM_BLOCKED));
}

TEST_F(ProgressionHelperTest, BlocksRubySanctumUntilTierFive)
{
    constexpr uint32 ICC_HEROIC = 31000;
    constexpr uint32 RS_ITEM = 32000;

    StoreItemTemplate(ICC_HEROIC, IP_LEVEL_WOTLK, 277);
    StoreItemTemplate(RS_ITEM, IP_LEVEL_WOTLK, 284);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(ICC_HEROIC, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_4));
    provider->RequireQuest(RS_ITEM, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_4), QUEST_STATUS_REWARDED);
    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5), QUEST_STATUS_NONE);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, ICC_HEROIC));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, RS_ITEM));

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5), QUEST_STATUS_REWARDED);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, RS_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksRubySanctum284GearUntilTierFive)
{
    constexpr uint32 RS_HARDMODE = 33000;

    StoreItemTemplate(RS_HARDMODE, IP_LEVEL_WOTLK, 284);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(RS_HARDMODE, GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5), QUEST_STATUS_NONE);
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, RS_HARDMODE));

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_WOTLK_TIER_5), QUEST_STATUS_REWARDED);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, RS_HARDMODE));
}

TEST_F(ProgressionHelperTest, UsesProgressionQuestConditions)
{
    constexpr uint32 BT_ITEM = 41000;

    StoreItemTemplate(BT_ITEM, IP_LEVEL_TBC, 154);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(BT_ITEM, GetProgressionQuestId(PROGRESSION_TBC_TIER_2));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_2), QUEST_STATUS_NONE);
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_2), QUEST_STATUS_REWARDED);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));
}

TEST_F(ProgressionHelperTest, IgnoresItemCapsConfig)
{
    constexpr uint32 BT_ITEM = 41000;

    StoreItemTemplate(BT_ITEM, IP_LEVEL_TBC, 154);

    auto provider = std::make_shared<TestProgressionConditionProvider>();
    provider->RequireQuest(BT_ITEM, GetProgressionQuestId(PROGRESSION_TBC_TIER_2));
    sIndividualProgression->SetProgressionConditionProvider(provider);

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_2), QUEST_STATUS_NONE);
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));

    LoadConfig({
        {"IndividualProgression.ItemCaps.10.MaxRequiredLevel", std::to_string(IP_LEVEL_TBC)},
        {"IndividualProgression.ItemCaps.10.MaxItemLevel", "150"}
    });

    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));

    provider->SetQuestStatus(GetProgressionQuestId(PROGRESSION_TBC_TIER_2), QUEST_STATUS_REWARDED);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));
}

TEST_F(ProgressionHelperTest, SharedHelperAllowsItemsWhenModuleDisabled)
{
    constexpr uint32 ICC_ITEM = 42000;

    StoreItemTemplate(ICC_ITEM, IP_LEVEL_WOTLK, 264);
    SetProgressionState(PROGRESSION_AQ);

    sIndividualProgression->enabled = false;

    EXPECT_TRUE(IsItemAllowedForProgression(player, ICC_ITEM));
}

TEST_F(ProgressionHelperTest, LoadsConditionsOnceAndReturnsEmptyForMissingItems)
{
    constexpr uint32 ITEM_WITH_CONDITION = 90000;
    constexpr uint32 ITEM_WITHOUT_CONDITION = 90001;

    int loadCount = 0;
    ProgressionConditionStore store([&loadCount]()
    {
        ++loadCount;
        std::vector<Condition> rows;
        Condition condition{};
        condition.SourceEntry = ITEM_WITH_CONDITION;
        rows.push_back(condition);
        return rows;
    });

    ConditionList const& first = store.GetConditionsForItem(ITEM_WITH_CONDITION);
    EXPECT_EQ(loadCount, 1);
    EXPECT_EQ(first.size(), 1u);

    ConditionList const& second = store.GetConditionsForItem(ITEM_WITH_CONDITION);
    EXPECT_EQ(loadCount, 1);
    EXPECT_EQ(second.size(), 1u);

    ConditionList const& missing = store.GetConditionsForItem(ITEM_WITHOUT_CONDITION);
    EXPECT_EQ(loadCount, 1);
    EXPECT_TRUE(missing.empty());
}
} // namespace
