# SPDX-License-Identifier: GPL-2.0-or-later
# Panero autotests (upstream Krusader 2.8.1 has none; see dev/DECISIONS.md D-005).
include(ECMAddTests)
find_package(Qt5 ${MIN_QT_VERSION} CONFIG REQUIRED Test)

ecm_add_test(paneroimporttest.cpp ../app/paneroimport.cpp
    TEST_NAME paneroimporttest
    LINK_LIBRARIES Qt5::Test KF5::ConfigCore KF5::I18n KF5::WidgetsAddons)
target_include_directories(paneroimporttest PRIVATE ${CMAKE_SOURCE_DIR}/app)

ecm_add_test(commandergesturetest.cpp ../app/Panel/PanelView/commandergesture.cpp
    TEST_NAME commandergesturetest
    LINK_LIBRARIES Qt5::Test Qt5::Core)
target_include_directories(commandergesturetest PRIVATE ${CMAKE_SOURCE_DIR}/app/Panel/PanelView)
