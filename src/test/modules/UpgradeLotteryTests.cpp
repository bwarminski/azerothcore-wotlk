// ABOUTME: Verifies xp-driven upgrade lottery respects progression limits for vendor baselines.
// ABOUTME: Ensures blocked items are excluded from vendor seed selection during upgrades.

#include "AchievementScript.h"
#include "IndividualProgression.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTestUtils.h"
#include "ProgressionConditionProvider.h"
#include "ProgressionItemRules.h"
#include "RandomItemMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <unordered_set>

using namespace testing;

class UpgradeLotteryTest_Accessor
{
public:
    static size_t SelectWeightedCandidateIndex(std::vector<float> const& cumulativeWeights, float targetWeight)
    {
        return RandomPlayerbotMgr::SelectWeightedCandidateIndex(cumulativeWeights, targetWeight);
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

    static void RegisterBot(Player* bot)
    {
        if (!bot)
            return;

        sRandomPlayerbotMgr->playerBots[bot->GetGUID()] = bot;
    }

    static void UnregisterBot(Player* bot)
    {
        if (!bot)
            return;

        sRandomPlayerbotMgr->playerBots.erase(bot->GetGUID());
    }

    static void ResetUpgradePassCalls(ObjectGuid::LowType guid)
    {
        sRandomPlayerbotMgr->upgradePassCalls[guid] = 0;
    }

    static void ResetUpgradeEpochState(ObjectGuid::LowType guid)
    {
        sRandomPlayerbotMgr->eventCache[guid]["upgrade_epoch"] = CachedEvent();
        sRandomPlayerbotMgr->eventCache[guid]["upgrade_epoch_progression"] = CachedEvent();
    }
};

namespace
{
class TestProgressionConditionProvider : public ProgressionConditionProvider
{
public:
    void BlockItem(uint32 itemId) { blockedItems.insert(itemId); }

    bool IsItemAllowed(Player* /*player*/, uint32 itemId) const override
    {
        return blockedItems.find(itemId) == blockedItems.end();
    }

private:
    std::unordered_set<uint32> blockedItems;
};

class UpgradeLotteryTest : public ::testing::Test
{
protected:
    static constexpr ObjectGuid::LowType botGuid = 9201;

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

        session = new WorldSession(1, "upgrade-lottery", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues(botGuid);
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetLevel(60);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 0, RACE_HUMAN);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, CLASS_WARRIOR);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 2, GENDER_MALE);

        originalProgressionEnabled = sIndividualProgression->enabled;
        sIndividualProgression->enabled = true;

        SetProgressionState(PROGRESSION_PRE_TBC);
        progressionProvider = std::make_shared<TestProgressionConditionProvider>();
        sIndividualProgression->SetProgressionConditionProvider(progressionProvider);

        LoadConfig();
        sRandomItemMgr->ResetVendorEquipmentCache();
    }

    void TearDown() override
    {
        sRandomPlayerbotMgr->ResetXpForUpgrade(botGuid);
        UpgradeLotteryTest_Accessor::ResetUpgradeEpochState(botGuid);
        UpgradeLotteryTest_Accessor::RemoveCurrentBot(botGuid);
        UpgradeLotteryTest_Accessor::UnregisterBot(player);
        PlayerbotTestUtils::RemoveFileIfExists(configPath);
        RestoreItemTemplates();
        sRandomItemMgr->ResetVendorEquipmentCache();

        for (auto const& vendorItem : vendorItems)
        {
            sObjectMgr->RemoveVendorItem(vendorItem.first, vendorItem.second, false);
        }

        sIndividualProgression->SetProgressionConditionProvider(nullptr);
        progressionProvider.reset();
        sIndividualProgression->enabled = originalProgressionEnabled;

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        worldMock = nullptr;
        session = nullptr;
        player = nullptr;
        vendorItems.clear();
    }

    void LoadConfig(uint32 limitGearExpansion = 0, uint32 biSWeeksAtEndgame = 0)
    {
        configPath = PlayerbotTestUtils::CreatePlayerbotConfig({
            {"AiPlayerbot.Enabled", "1"},
            {"AiPlayerbot.SkipInitialSetup", "1"},
            {"AiPlayerbot.XpUpgradeEnabled", "1"},
            {"AiPlayerbot.XpUpgradeChunk", "1000"},
            {"AiPlayerbot.VendorSeedEnabled", "1"},
            {"AiPlayerbot.LimitGearExpansion", std::to_string(limitGearExpansion)},
            {"AiPlayerbot.BiSWeeksAtEndgame", std::to_string(biSWeeksAtEndgame)},
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

    void ResetItemTemplates()
    {
        auto store = const_cast<ItemTemplateContainer*>(sObjectMgr->GetItemTemplateStore());
        store->clear();

        auto fastStore = const_cast<std::vector<ItemTemplate*>*>(sObjectMgr->GetItemTemplateStoreFast());
        fastStore->clear();
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

    void SeedChestItems(uint32 baseEntry, uint32 count, uint32 requiredLevel, uint32 itemLevel)
    {
        for (uint32 i = 0; i < count; ++i)
        {
            StoreArmorTemplate(baseEntry + i, INVTYPE_CHEST, ITEM_SUBCLASS_ARMOR_PLATE, requiredLevel, itemLevel);
        }
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

    void BlockProgressionItem(uint32 itemId)
    {
        if (progressionProvider)
        {
            progressionProvider->BlockItem(itemId);
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
    bool originalProgressionEnabled = false;
    std::shared_ptr<TestProgressionConditionProvider> progressionProvider;
};

TEST_F(UpgradeLotteryTest, VendorBaselineSkipsProgressionBlockedItems)
{
    constexpr uint32 VENDOR_ENTRY = 93001;
    constexpr uint32 ALLOWED_ITEM = 46000;
    constexpr uint32 BLOCKED_ITEM = 46001;

    BlockProgressionItem(BLOCKED_ITEM);
    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(ALLOWED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 100, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(BLOCKED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 200, ITEM_MOD_STRENGTH, 200);
    AddVendorItem(VENDOR_ENTRY, ALLOWED_ITEM);
    AddVendorItem(VENDOR_ENTRY, BLOCKED_ITEM);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    EXPECT_TRUE(sIndividualProgression->enabled);
    EXPECT_EQ(player->GetPlayerSetting("mod-individual-progression", SETTING_PROGRESSION_STATE).value,
        PROGRESSION_PRE_TBC);
    EXPECT_FALSE(IsItemAllowedForProgression(player, BLOCKED_ITEM));

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"progression-vendor", 100, true, std::nullopt});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), ALLOWED_ITEM);
}

TEST_F(UpgradeLotteryTest, SharedHelperBlocksItemsBelowExpansionIdThreshold)
{
    constexpr uint32 VENDOR_ENTRY = 93002;
    constexpr uint32 ALLOWED_ITEM = 22000;
    constexpr uint32 BLOCKED_ITEM = 22001;

    LoadConfig(1);
    BlockProgressionItem(BLOCKED_ITEM);

    ClearSlot(EQUIPMENT_SLOT_HEAD);
    StoreArmorTemplate(ALLOWED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 100, ITEM_MOD_STRENGTH, 10);
    StoreArmorTemplate(BLOCKED_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 60, 200, ITEM_MOD_STRENGTH, 200);
    AddVendorItem(VENDOR_ENTRY, ALLOWED_ITEM);
    AddVendorItem(VENDOR_ENTRY, BLOCKED_ITEM);

    sRandomItemMgr->ResetVendorEquipmentCache();
    sRandomItemMgr->RebuildEquipmentCache();

    EXPECT_FALSE(IsItemAllowedForProgression(player, BLOCKED_ITEM));

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"progression-vendor", 100, true, std::nullopt});

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), ALLOWED_ITEM);
}

TEST_F(UpgradeLotteryTest, WeightedPercentilePrefersMidTierAtLowerRolls)
{
    constexpr uint32 BASE_ENTRY = 61000;
    constexpr uint32 OUTLIER_ENTRY = 61150;

    ResetItemTemplates();
    SetProgressionState(PROGRESSION_WOTLK_TIER_5);
    player->SetLevel(DEFAULT_MAX_LEVEL);

    SeedChestItems(BASE_ENTRY, 100, DEFAULT_MAX_LEVEL, 200);
    StoreArmorTemplate(OUTLIER_ENTRY, INVTYPE_CHEST, ITEM_SUBCLASS_ARMOR_PLATE, DEFAULT_MAX_LEVEL, 280);
    sRandomItemMgr->RebuildEquipmentCache();
    ClearSlot(EQUIPMENT_SLOT_CHEST);

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"weighted-mid", 90, false, DEFAULT_MAX_LEVEL});

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_NE(chestItem->GetEntry(), OUTLIER_ENTRY);
}

TEST_F(UpgradeLotteryTest, WeightedPercentileSelectsOutlierAtTopRolls)
{
    constexpr uint32 BASE_ENTRY = 62000;
    constexpr uint32 OUTLIER_ENTRY = 62150;

    ResetItemTemplates();
    SetProgressionState(PROGRESSION_WOTLK_TIER_5);
    player->SetLevel(DEFAULT_MAX_LEVEL);

    SeedChestItems(BASE_ENTRY, 100, DEFAULT_MAX_LEVEL, 200);
    StoreArmorTemplate(OUTLIER_ENTRY, INVTYPE_CHEST, ITEM_SUBCLASS_ARMOR_PLATE, DEFAULT_MAX_LEVEL, 280);
    sRandomItemMgr->RebuildEquipmentCache();
    ClearSlot(EQUIPMENT_SLOT_CHEST);

    sRandomPlayerbotMgr->RunGearUpgradePass(player,
        RandomPlayerbotMgr::UpgradeContext{"weighted-top", 99, false, DEFAULT_MAX_LEVEL});

    Item* chestItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_CHEST);
    ASSERT_NE(chestItem, nullptr);
    EXPECT_EQ(chestItem->GetEntry(), OUTLIER_ENTRY);
}

TEST_F(UpgradeLotteryTest, CumulativeSelectionMatchesRepresentativeTargets)
{
    std::vector<float> cumulativeWeights = {1.0f, 5.0f, 10.0f};

    EXPECT_EQ(UpgradeLotteryTest_Accessor::SelectWeightedCandidateIndex(cumulativeWeights, 0.0f), 0u);
    EXPECT_EQ(UpgradeLotteryTest_Accessor::SelectWeightedCandidateIndex(cumulativeWeights, 5.0f), 1u);
    EXPECT_EQ(UpgradeLotteryTest_Accessor::SelectWeightedCandidateIndex(cumulativeWeights, 10.0f), 2u);
}

TEST_F(UpgradeLotteryTest, EndgameEpochBumpRunsUpgradePassesForEligibleBots)
{
    UpgradeLotteryTest_Accessor::RegisterBot(player);
    UpgradeLotteryTest_Accessor::AddCurrentBot(botGuid);
    UpgradeLotteryTest_Accessor::ResetUpgradePassCalls(botGuid);

    SetProgressionState(PROGRESSION_NAXX40);
    player->SetLevel(IP_LEVEL_VANILLA);

    LoadConfig(0, 1);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 1u);

    LoadConfig(0, 3);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 3u);
}

TEST_F(UpgradeLotteryTest, EndgameEpochResetsOnProgressionChange)
{
    UpgradeLotteryTest_Accessor::RegisterBot(player);
    UpgradeLotteryTest_Accessor::AddCurrentBot(botGuid);
    UpgradeLotteryTest_Accessor::ResetUpgradePassCalls(botGuid);

    SetProgressionState(PROGRESSION_NAXX40);
    player->SetLevel(IP_LEVEL_VANILLA);
    LoadConfig(0, 1);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 1u);

    UpgradeLotteryTest_Accessor::ResetUpgradePassCalls(botGuid);
    SetProgressionState(PROGRESSION_TBC_TIER_2);
    player->SetLevel(IP_LEVEL_TBC);

    LoadConfig(0, 2);
    EXPECT_EQ(sRandomPlayerbotMgr->GetUpgradePassCallCount(botGuid), 2u);
}
} // namespace
