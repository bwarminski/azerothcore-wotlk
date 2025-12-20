// ABOUTME: Verifies xp-driven upgrade behavior for playerbots.
// ABOUTME: Covers backfill simulation, percentile selection, and expansion gating.

#include "AchievementScript.h"
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

#include <ctime>
#include <limits>

using namespace testing;

namespace
{
class XpBackfillTest : public ::testing::Test
{
protected:
    static constexpr ObjectGuid::LowType primaryBotGuid = 5001;

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

        session = new WorldSession(1, "xp-backfill", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues(primaryBotGuid);
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 0, RACE_HUMAN);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, CLASS_WARRIOR);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 2, GENDER_MALE);

        sRandomItemMgr->ResetVendorEquipmentCache();
    }

    void TearDown() override
    {
        sRandomPlayerbotMgr->ResetXpForUpgrade(primaryBotGuid);
        PlayerbotTestUtils::RemoveFileIfExists(configPath);
        RestoreItemTemplates();
        sRandomItemMgr->ResetVendorEquipmentCache();

        for (auto const& vendorItem : vendorItems)
        {
            sObjectMgr->RemoveVendorItem(vendorItem.first, vendorItem.second, false);
        }

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        worldMock = nullptr;
        session = nullptr;
        player = nullptr;
        vendorItems.clear();
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

    static uint32 TotalXpToLevel(uint8 level)
    {
        uint32 total = 0;
        for (uint8 idx = 1; idx < level; ++idx)
        {
            uint32 xpForLevel = sObjectMgr->GetXPForLevel(idx);
            if (!xpForLevel)
            {
                xpForLevel = static_cast<uint32>(idx) * 1000;
            }
            total += xpForLevel;
        }
        return total;
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

    void StoreArmorTemplate(uint32 entry, InventoryType inventoryType, uint32 subClass, uint32 requiredLevel,
        uint32 itemLevel, uint32 statType = 0, int32 statValue = 0)
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        ItemTemplate& proto = (*store)[entry];
        proto = ItemTemplate();
        proto.ItemId = entry;
        proto.Class = ITEM_CLASS_ARMOR;
        proto.SubClass = subClass;
        proto.InventoryType = inventoryType;
        proto.RequiredLevel = requiredLevel;
        proto.ItemLevel = itemLevel;
        proto.Quality = ITEM_QUALITY_NORMAL;
        proto.AllowableClass = -1;
        proto.AllowableRace = -1;
        proto.BuyCount = 1;
        proto.SellPrice = 0;
        proto.BuyPrice = 0;
        proto.Duration = 0;
        proto.StatsCount = statType ? 1 : 0;
        if (statType)
        {
            proto.ItemStat[0].ItemStatType = statType;
            proto.ItemStat[0].ItemStatValue = statValue;
        }

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        if (fastStore->size() <= entry)
        {
            fastStore->resize(entry + 1, nullptr);
        }
        (*fastStore)[entry] = &proto;
    }

    void AddVendorItem(uint32 vendorEntry, uint32 itemId)
    {
        sObjectMgr->AddVendorItem(vendorEntry, itemId, 0, 0, 0, false);
        vendorItems.emplace_back(vendorEntry, itemId);
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
    std::vector<std::pair<uint32, uint32>> vendorItems;
};

TEST_F(XpBackfillTest, SimulatesUpgradeAndRemainderForLevelSet)
{
    player->SetLevel(8);
    uint32 chunkSize = TotalXpToLevel(7) + 1;
    LoadConfig(chunkSize);
    sPlayerbotAIConfig->vendorSeedEnabled = false;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 LOW_LEVEL_ITEM = 45000;
    constexpr uint32 HIGH_LEVEL_ITEM = 45001;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(LOW_LEVEL_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 7, 20, ITEM_MOD_STRENGTH, 20);
    StoreArmorTemplate(HIGH_LEVEL_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 8, 40, ITEM_MOD_STRENGTH, 80);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    sRandomPlayerbotMgr->ApplyLevelBasedUpgradeProgress(player);

    uint32 totalXp = TotalXpToLevel(player->GetLevel());
    uint32 expectedUpgrades = totalXp / chunkSize;
    uint32 expectedRemainder = totalXp % chunkSize;

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), LOW_LEVEL_ITEM);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(primaryBotGuid), expectedUpgrades);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), expectedRemainder);
}

TEST_F(XpBackfillTest, BracketLevelResetUsesBackfillRemainder)
{
    LoadConfig(2000);
    sPlayerbotAIConfig->vendorSeedEnabled = true;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 VENDOR_ENTRY = 91001;
    constexpr uint32 BASELINE_ITEM = 45010;
    constexpr uint32 UPGRADE_ITEM = 45011;
    constexpr uint32 BASELINE_CHEST = 45013;
    constexpr uint32 EXISTING_CHEST = 45012;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    ClearSlot(EQUIPMENT_SLOT_CHEST);
    StoreArmorTemplate(BASELINE_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 9, 18, ITEM_MOD_STRENGTH, 12);
    StoreArmorTemplate(UPGRADE_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 9, 30, ITEM_MOD_STRENGTH, 80);
    StoreArmorTemplate(BASELINE_CHEST, INVTYPE_CHEST, ITEM_SUBCLASS_ARMOR_MAIL, 9, 20, ITEM_MOD_STAMINA, 8);
    StoreArmorTemplate(EXISTING_CHEST, INVTYPE_CHEST, ITEM_SUBCLASS_ARMOR_MAIL, 60, 80, ITEM_MOD_STAMINA, 80);
    AddVendorItem(VENDOR_ENTRY, BASELINE_ITEM);
    AddVendorItem(VENDOR_ENTRY, BASELINE_CHEST);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    player->SetLevel(60);
    player->StoreNewItemInBestSlots(EXISTING_CHEST, 1);

    uint8 newLevel = 10;
    sRandomPlayerbotMgr->ApplyBracketLevelReset(player, newLevel);

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), UPGRADE_ITEM);

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_EQ(chestItem->GetEntry(), BASELINE_CHEST);

    uint32 totalXp = TotalXpToLevel(newLevel);
    uint32 expectedUpgrades = totalXp / 2000;
    uint32 expectedRemainder = totalXp % 2000;

    EXPECT_EQ(player->GetLevel(), newLevel);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(primaryBotGuid), expectedUpgrades);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), expectedRemainder);
}

TEST_F(XpBackfillTest, BracketLevelResetSeedsBaselineWhenNoChunksCrossed)
{
    LoadConfig(7000);
    sPlayerbotAIConfig->vendorSeedEnabled = true;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 VENDOR_ENTRY = 91002;
    constexpr uint32 BASELINE_ITEM = 45014;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(BASELINE_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 3, 12, ITEM_MOD_STRENGTH, 5);
    AddVendorItem(VENDOR_ENTRY, BASELINE_ITEM);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    player->SetLevel(1);
    uint8 newLevel = 3;
    sRandomPlayerbotMgr->ApplyBracketLevelReset(player, newLevel);

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), BASELINE_ITEM);

    uint32 totalXp = TotalXpToLevel(newLevel);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(primaryBotGuid), 1u);
    EXPECT_EQ(sRandomPlayerbotMgr->GetXpSinceLastUpgrade(primaryBotGuid), totalXp);
}

TEST_F(XpBackfillTest, RunGearUpgradePassSelectsNearestPercentileRank)
{
    LoadConfig(1000);
    player->SetLevel(5);
    sPlayerbotAIConfig->vendorSeedEnabled = false;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 LOW_ITEM = 45100;
    constexpr uint32 MID_ITEM = 45101;
    constexpr uint32 HIGH_ITEM = 45102;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(LOW_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(MID_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 20);
    StoreArmorTemplate(HIGH_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 30);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    sRandomPlayerbotMgr->RunGearUpgradePass(player, RandomPlayerbotMgr::UpgradeContext{"percentile", 50, false});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), MID_ITEM);
}

TEST_F(XpBackfillTest, RunGearUpgradePassSelectsWorstAtZeroPercentile)
{
    LoadConfig(1000);
    player->SetLevel(5);
    sPlayerbotAIConfig->vendorSeedEnabled = false;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 LOW_ITEM = 45200;
    constexpr uint32 MID_ITEM = 45201;
    constexpr uint32 HIGH_ITEM = 45202;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(LOW_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(MID_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 20);
    StoreArmorTemplate(HIGH_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 30);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    sRandomPlayerbotMgr->RunGearUpgradePass(player, RandomPlayerbotMgr::UpgradeContext{"percentile-low", 0, false});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), LOW_ITEM);
}

TEST_F(XpBackfillTest, RunGearUpgradePassSelectsBestAtMaxPercentile)
{
    LoadConfig(1000);
    player->SetLevel(5);
    sPlayerbotAIConfig->vendorSeedEnabled = false;
    sPlayerbotAIConfig->limitGearExpansion = 0;
    constexpr uint32 LOW_ITEM = 45210;
    constexpr uint32 MID_ITEM = 45211;
    constexpr uint32 HIGH_ITEM = 45212;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(LOW_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(MID_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 20);
    StoreArmorTemplate(HIGH_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 5, 20, ITEM_MOD_STRENGTH, 30);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    sRandomPlayerbotMgr->RunGearUpgradePass(player, RandomPlayerbotMgr::UpgradeContext{"percentile-high", 100, false});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), HIGH_ITEM);
}

TEST_F(XpBackfillTest, RunGearUpgradePassRespectsExpansionLimit)
{
    LoadConfig(1000);
    player->SetLevel(60);
    sPlayerbotAIConfig->vendorSeedEnabled = false;
    sPlayerbotAIConfig->limitGearExpansion = 1;
    constexpr uint32 ALLOWED_ITEM = 23727;
    constexpr uint32 BLOCKED_ITEM = 23728;

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(ALLOWED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 50, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(BLOCKED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 60, ITEM_MOD_STRENGTH, 50);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    sRandomPlayerbotMgr->RunGearUpgradePass(player, RandomPlayerbotMgr::UpgradeContext{"expansion", 0, false});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), ALLOWED_ITEM);
}
} // namespace
