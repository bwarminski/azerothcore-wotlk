// ABOUTME: Verifies endgame BiS ramp behavior for xp-driven gear upgrades.
// ABOUTME: Ensures percentile floors and over-cap odds apply only at endgame caps.

#include "AchievementScript.h"
#include "IndividualProgression.h"
#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTestUtils.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace testing;

namespace
{
class BiSRampTest : public ::testing::Test
{
protected:
    static constexpr ObjectGuid::LowType botGuid = 9401;

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

        CaptureItemTemplates();

        session = new WorldSession(1, "bis-ramp", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues(botGuid);
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 0, RACE_HUMAN);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, CLASS_WARRIOR);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 2, GENDER_MALE);

        sRandomItemMgr->ResetVendorEquipmentCache();
    }

    void TearDown() override
    {
        PlayerbotTestUtils::RemoveFileIfExists(configPath);
        RestoreItemTemplates();
        sRandomItemMgr->RebuildEquipmentCache();
        sRandomItemMgr->ResetVendorEquipmentCache();

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        worldMock = nullptr;
        session = nullptr;
        player = nullptr;
    }

    void LoadConfig(uint32 biSWeeks)
    {
        configPath = PlayerbotTestUtils::CreatePlayerbotConfig({
            {"AiPlayerbot.Enabled", "1"},
            {"AiPlayerbot.SkipInitialSetup", "1"},
            {"AiPlayerbot.XpUpgradeEnabled", "1"},
            {"AiPlayerbot.XpUpgradeChunk", "1000"},
            {"AiPlayerbot.VendorSeedEnabled", "0"},
            {"AiPlayerbot.ProgressionState", std::to_string(PROGRESSION_WOTLK_TIER_5)},
            {"AiPlayerbot.BiSWeeksAtEndgame", std::to_string(biSWeeks)},
        });
        sConfigMgr->Configure(configPath, std::vector<std::string>());
        sConfigMgr->LoadAppConfigs();
        PlayerbotAIConfig::instance()->Initialize();
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
    }

    void RestoreItemTemplates()
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        *store = originalTemplates;

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        *fastStore = originalFastTemplates;
    }

    void StoreArmorTemplate(uint32 entry, uint32 requiredLevel, uint32 itemLevel)
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        ItemTemplate& proto = (*store)[entry];
        proto = ItemTemplate();
        proto.ItemId = entry;
        proto.Class = ITEM_CLASS_ARMOR;
        proto.SubClass = ITEM_SUBCLASS_ARMOR_PLATE;
        proto.InventoryType = INVTYPE_CHEST;
        proto.RequiredLevel = requiredLevel;
        proto.ItemLevel = itemLevel;
        proto.Quality = ITEM_QUALITY_NORMAL;
        proto.AllowableClass = -1;
        proto.AllowableRace = -1;
        proto.BuyCount = 1;
        proto.SellPrice = 0;
        proto.BuyPrice = 0;
        proto.Duration = 0;

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        if (fastStore->size() <= entry)
        {
            fastStore->resize(entry + 1, nullptr);
        }
        (*fastStore)[entry] = &proto;
    }

    void SeedChestItems(uint32 baseEntry, uint8 count, uint32 requiredLevel, uint32 baseItemLevel)
    {
        for (uint8 i = 0; i < count; ++i)
        {
            StoreArmorTemplate(baseEntry + i, requiredLevel, baseItemLevel + i);
        }
    }

    void ClearSlot(uint8 slot)
    {
        if (Item* existing = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
        {
            player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
        }
    }

    std::string configPath;
    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
    ItemTemplateContainer originalTemplates;
    std::vector<ItemTemplate*> originalFastTemplates;
};

TEST_F(BiSRampTest, AppliesPercentileFloorAtEndgameCap)
{
    LoadConfig(3);
    player->SetLevel(DEFAULT_MAX_LEVEL);
    SetProgressionState(PROGRESSION_WOTLK_TIER_5);

    SeedChestItems(51000, 10, DEFAULT_MAX_LEVEL, 200);
    sRandomItemMgr->RebuildEquipmentCache();
    ClearSlot(EQUIPMENT_SLOT_CHEST);

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"bis-floor", 10, false, std::nullopt});

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_EQ(chestItem->GetEntry(), 51007u);
}

TEST_F(BiSRampTest, UsesOvercapChanceForTopTierSelection)
{
    LoadConfig(4);
    player->SetLevel(DEFAULT_MAX_LEVEL);
    SetProgressionState(PROGRESSION_WOTLK_TIER_5);

    SeedChestItems(52000, 10, DEFAULT_MAX_LEVEL, 200);
    sRandomItemMgr->RebuildEquipmentCache();
    ClearSlot(EQUIPMENT_SLOT_CHEST);

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"bis-overcap", 6, false, std::nullopt});

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_EQ(chestItem->GetEntry(), 52009u);
}

TEST_F(BiSRampTest, IgnoresRampWhenNotAtMaxLevel)
{
    LoadConfig(3);
    player->SetLevel(DEFAULT_MAX_LEVEL - 1);
    SetProgressionState(PROGRESSION_WOTLK_TIER_5);

    SeedChestItems(53000, 10, DEFAULT_MAX_LEVEL - 1, 200);
    sRandomItemMgr->RebuildEquipmentCache();
    ClearSlot(EQUIPMENT_SLOT_CHEST);

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"bis-inactive", 0, false, std::nullopt});

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_EQ(chestItem->GetEntry(), 53000u);
}
} // namespace
