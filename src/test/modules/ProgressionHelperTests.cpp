// ABOUTME: Validates progression-based item allowances for individual progression helpers.
// ABOUTME: Uses mocked world and session to assert gating on item requirements per progression state.

#include "IndividualProgression.h"
#include "ItemTemplate.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
};

TEST_F(ProgressionHelperTest, BlocksItemsAboveCurrentExpansion)
{
    constexpr uint32 VANILLA_ITEM = 10000;
    constexpr uint32 TBC_ITEM = 20000;

    StoreItemTemplate(VANILLA_ITEM, 60, 70);
    StoreItemTemplate(TBC_ITEM, IP_LEVEL_TBC, 164);

    SetProgressionState(PROGRESSION_AQ);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, VANILLA_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, TBC_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksLateTbcGearBeforeSunwell)
{
    constexpr uint32 BT_ITEM = 21000;
    constexpr uint32 SUNWELL_ITEM = 22000;

    StoreItemTemplate(BT_ITEM, IP_LEVEL_TBC, 152);
    StoreItemTemplate(SUNWELL_ITEM, IP_LEVEL_TBC, 164);

    SetProgressionState(PROGRESSION_TBC_TIER_2);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, BT_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, SUNWELL_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksIccGearBeforeIccPhase)
{
    constexpr uint32 ULD_ITEM = 23000;
    constexpr uint32 ICC_ITEM = 24000;

    StoreItemTemplate(ULD_ITEM, IP_LEVEL_WOTLK, 226);
    StoreItemTemplate(ICC_ITEM, IP_LEVEL_WOTLK, 264);

    SetProgressionState(PROGRESSION_WOTLK_TIER_1);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, ULD_ITEM));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, ICC_ITEM));
}

TEST_F(ProgressionHelperTest, AllowsWrathItemsAtEndProgression)
{
    constexpr uint32 WOTLK_ITEM = 30000;
    StoreItemTemplate(WOTLK_ITEM, IP_LEVEL_WOTLK, 200);

    SetProgressionState(PROGRESSION_WOTLK_TIER_2);

    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, WOTLK_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksRubySanctumUntilTierFive)
{
    constexpr uint32 ICC_HEROIC = 31000;
    constexpr uint32 RS_ITEM = 32000;

    StoreItemTemplate(ICC_HEROIC, IP_LEVEL_WOTLK, 277);
    StoreItemTemplate(RS_ITEM, IP_LEVEL_WOTLK, 284);

    SetProgressionState(PROGRESSION_WOTLK_TIER_4);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, ICC_HEROIC));
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, RS_ITEM));

    SetProgressionState(PROGRESSION_WOTLK_TIER_5);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, RS_ITEM));
}

TEST_F(ProgressionHelperTest, BlocksRubySanctum284GearUntilTierFive)
{
    constexpr uint32 RS_HARDMODE = 33000;

    StoreItemTemplate(RS_HARDMODE, IP_LEVEL_WOTLK, 284);

    SetProgressionState(PROGRESSION_WOTLK_TIER_4);
    EXPECT_FALSE(sIndividualProgression->IsItemAllowedForProgression(player, RS_HARDMODE));

    SetProgressionState(PROGRESSION_WOTLK_TIER_5);
    EXPECT_TRUE(sIndividualProgression->IsItemAllowedForProgression(player, RS_HARDMODE));
}
} // namespace
