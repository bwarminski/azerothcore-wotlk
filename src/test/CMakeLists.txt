# ABOUTME: Defines unit test targets and CTest entries for the project.
# ABOUTME: Collects sources and registers module-specific test filters.
#
# This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
CollectSourceFiles(
        ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE_SOURCES
)

include_directories(
        "mocks"
        "${CMAKE_SOURCE_DIR}/modules"
)

add_executable(
        unit_tests
        ${PRIVATE_SOURCES}
)

target_link_libraries(
        unit_tests
        game
        gtest_main
        gmock_main
        game-interface
        modules
)

add_test(
        NAME
        unit
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests
)

add_test(
        NAME
        ProgressionHelperTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=ProgressionHelperTest.*
)

add_custom_target(mod-individual-progression-tests DEPENDS unit_tests)

add_test(
        NAME
        PlayerbotConfigTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=PlayerbotConfigTest.*
)

add_test(
        NAME
        VendorCacheTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=VendorCacheTest.*
)

add_test(
        NAME
        XpTrackingTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=XpTrackingTest.*
)

add_test(
        NAME
        XpBackfillTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=XpBackfillTest.*
)

add_test(
        NAME
        XpHookTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=XpHookTest.*
)

add_test(
        NAME
        UpgradeLotteryTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=UpgradeLotteryTest.*
)

add_test(
        NAME
        BiSRampTests
        COMMAND
        ${CMAKE_BINARY_DIR}/src/test/unit_tests --gtest_filter=BiSRampTest.*
)

add_custom_target(mod-playerbots-tests DEPENDS unit_tests)
