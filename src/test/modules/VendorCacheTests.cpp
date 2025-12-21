// ABOUTME: Validates vendor gear cache selection for playerbots and baseline equip behavior.
// ABOUTME: Ensures vendor items seed empty gear slots during initial equipment generation.

#include "AchievementScript.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "RandomItemMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldMock.h"
#include "WorldSession.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace testing;

namespace
{
class VendorCacheTest : public ::testing::Test
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

        static std::string emptyString;
        ON_CALL(*worldMock, GetDataPath()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetRealmName()).WillByDefault(ReturnRef(emptyString));
        ON_CALL(*worldMock, GetDefaultDbcLocale()).WillByDefault(Return(LOCALE_enUS));
        ON_CALL(*worldMock, getRate(_)).WillByDefault(Return(1.0f));
        ON_CALL(*worldMock, getBoolConfig(_)).WillByDefault(Return(false));
        ON_CALL(*worldMock, getIntConfig(_)).WillByDefault(Return(DEFAULT_MAX_LEVEL));

        CaptureItemTemplates();

        session = new WorldSession(1, "vendor-cache", 0, nullptr, SEC_PLAYER, EXPANSION_WRATH_OF_THE_LICH_KING, 0,
            LOCALE_enUS, 0, false, false, 0);

        player = new TestPlayer(session);
        player->ForceInitValues();
        session->SetPlayer(player);
        player->SetSession(session);
        player->SetLevel(30);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 0, RACE_HUMAN);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 1, CLASS_WARRIOR);
        player->SetByteValue(UNIT_FIELD_BYTES_0, 2, GENDER_MALE);

        sPlayerbotAIConfig->randomGearLoweringChance = 0.0f;
        sPlayerbotAIConfig->twoRoundsGearInit = false;
        sPlayerbotAIConfig->limitGearExpansion = false;
        sPlayerbotAIConfig->vendorSeedEnabled = true;

        sRandomItemMgr->ResetVendorEquipmentCache();
    }

    void TearDown() override
    {
        RestoreItemTemplates();

        for (auto const& vendorItem : vendorItems)
        {
            sObjectMgr->RemoveVendorItem(vendorItem.first, vendorItem.second, false);
        }

        sRandomItemMgr->ResetVendorEquipmentCache();

        IWorld* currentWorld = sWorld.release();
        delete currentWorld;
        sWorld.reset(originalWorld);
        originalWorld = nullptr;
        worldMock = nullptr;
        session = nullptr;
        player = nullptr;
        vendorItems.clear();
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

    IWorld* originalWorld = nullptr;
    NiceMock<WorldMock>* worldMock = nullptr;
    WorldSession* session = nullptr;
    TestPlayer* player = nullptr;
    ItemTemplateContainer originalTemplates;
    std::vector<ItemTemplate*> originalFastTemplates;
    std::vector<std::pair<uint32, uint32>> vendorItems;
};

TEST_F(VendorCacheTest, PicksHighestVendorItemUpToLevel)
{
    constexpr uint32 VENDOR_ENTRY = 90000;
    constexpr uint32 LOWER_ITEM = 40000;
    constexpr uint32 HIGH_ITEM = 40001;

    StoreArmorTemplate(LOWER_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 20, 30);
    StoreArmorTemplate(HIGH_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 30, 40);
    AddVendorItem(VENDOR_ENTRY, LOWER_ITEM);
    AddVendorItem(VENDOR_ENTRY, HIGH_ITEM);

    sRandomItemMgr->ResetVendorEquipmentCache();

    auto getLastVendorItem = [](std::vector<uint32> const& items) -> uint32
    {
        return items.empty() ? 0u : items.back();
    };

    EXPECT_EQ(getLastVendorItem(sRandomItemMgr->GetVendorItems(CLASS_WARRIOR, 25, INVTYPE_HEAD)), LOWER_ITEM);
    EXPECT_EQ(getLastVendorItem(sRandomItemMgr->GetVendorItems(CLASS_WARRIOR, 35, INVTYPE_HEAD)), HIGH_ITEM);
}

TEST_F(VendorCacheTest, InitEquipmentUsesVendorBaselineWhenSlotEmpty)
{
    constexpr uint32 VENDOR_ENTRY = 90001;
    constexpr uint32 BASELINE_ITEM = 41000;

    StoreArmorTemplate(BASELINE_ITEM, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 20, 30);
    AddVendorItem(VENDOR_ENTRY, BASELINE_ITEM);

    sRandomItemMgr->ResetVendorEquipmentCache();
    auto vendorItemsForLevel = sRandomItemMgr->GetVendorItems(player->getClass(), player->GetLevel(), INVTYPE_HEAD);
    ASSERT_FALSE(vendorItemsForLevel.empty());
    EXPECT_EQ(vendorItemsForLevel.back(), BASELINE_ITEM);

    PlayerbotFactory factory(player, player->GetLevel(), ITEM_QUALITY_NORMAL, 0);
    if (Item* existing = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD))
    {
        player->DestroyItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD, true);
    }
    factory.InitEquipment(false, false);

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), BASELINE_ITEM);
}

TEST_F(VendorCacheTest, VendorBaselineUsesStatWeightNotItemLevel)
{
    constexpr uint32 VENDOR_ENTRY = 90002;
    constexpr uint32 HIGH_ITEMLEVEL_LOW_STAT = 42000;
    constexpr uint32 LOWER_ITEMLEVEL_HIGH_STAT = 42001;

    StoreArmorTemplate(HIGH_ITEMLEVEL_LOW_STAT, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 30, 60);
    StoreArmorTemplate(LOWER_ITEMLEVEL_HIGH_STAT, INVTYPE_HEAD, ITEM_SUBCLASS_ARMOR_MAIL, 30, 45, ITEM_MOD_STRENGTH,
        40);
    AddVendorItem(VENDOR_ENTRY, HIGH_ITEMLEVEL_LOW_STAT);
    AddVendorItem(VENDOR_ENTRY, LOWER_ITEMLEVEL_HIGH_STAT);

    sRandomItemMgr->ResetVendorEquipmentCache();

    PlayerbotFactory factory(player, player->GetLevel(), ITEM_QUALITY_NORMAL, 0);
    if (Item* existing = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD))
    {
        player->DestroyItem(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD, true);
    }

    factory.InitEquipment(false, false);

    Item* headItem = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HEAD);
    ASSERT_NE(headItem, nullptr);
    EXPECT_EQ(headItem->GetEntry(), LOWER_ITEMLEVEL_HIGH_STAT);
}
} // namespace
