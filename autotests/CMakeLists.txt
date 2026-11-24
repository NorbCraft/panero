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

ecm_add_test(panerothemetest.cpp ../app/Panel/panerotheme.cpp
    TEST_NAME panerothemetest
    LINK_LIBRARIES Qt5::Test Qt5::Widgets KF5::ConfigCore)
target_include_directories(panerothemetest PRIVATE ${CMAKE_SOURCE_DIR}/app/Panel ${CMAKE_SOURCE_DIR}/app)

ecm_add_test(paneropathstest.cpp ../app/Panel/paneropaths.cpp
    TEST_NAME paneropathstest
    LINK_LIBRARIES Qt5::Test Qt5::Core PaneroSlowPaths)
target_include_directories(paneropathstest PRIVATE ${CMAKE_SOURCE_DIR}/app/Panel)

ecm_add_test(paneroportaldragtest.cpp ../app/Panel/paneroportaldrag.cpp
    TEST_NAME paneroportaldragtest
    LINK_LIBRARIES Qt5::Test Qt5::Core Qt5::DBus)
target_include_directories(paneroportaldragtest PRIVATE ${CMAKE_SOURCE_DIR}/app/Panel)

if(NOT WIN32) # Cloud is not offered on Windows (rclone mount needs FUSE)
# Cloud backend (rclone / KIO servers). The mount test needs rclone in PATH and
# /dev/fuse; it skips otherwise. All state lives in temporary HOME/XDG dirs.
ecm_add_test(panerocloudtest.cpp ../app/paneropackageinstaller.cpp
    TEST_NAME panerocloudtest
    LINK_LIBRARIES Qt5::Test Qt5::DBus Qt5::Concurrent Cloud)
target_include_directories(panerocloudtest PRIVATE ${CMAKE_SOURCE_DIR}/app/Cloud ${CMAKE_SOURCE_DIR}/app ${CMAKE_SOURCE_DIR}/app/FileSystem)

# Security/integration tests of the cloud feature: skipped unless run through
# dev/scripts/cloud-security-test.sh (private D-Bus session and keyring, temp HOME).
ecm_add_test(panerocloudsecuritytest.cpp ../app/paneropackageinstaller.cpp
    TEST_NAME panerocloudsecuritytest
    LINK_LIBRARIES Qt5::Test Qt5::DBus Qt5::Network Cloud)
target_include_directories(panerocloudsecuritytest PRIVATE ${CMAKE_SOURCE_DIR}/app/Cloud ${CMAKE_SOURCE_DIR}/app)
add_dependencies(panerocloudsecuritytest panero-cloud-key)
endif()

# Which mounts get a drive button (D-019): pure classification of mount table entries.
ecm_add_test(panerodrivestest.cpp ../app/GUI/panerodrivefilter.cpp
    TEST_NAME panerodrivestest
    LINK_LIBRARIES Qt5::Test Qt5::Core KF5::I18n)
target_include_directories(panerodrivestest PRIVATE ${CMAKE_SOURCE_DIR}/app/GUI)

# Multi-Rename rules (D-019): Total Commander style patterns, search & replace, conflicts, order.
ecm_add_test(paneromultirenametest.cpp ../app/Dialogs/paneromultirenamerules.cpp
    TEST_NAME paneromultirenametest
    LINK_LIBRARIES Qt5::Test Qt5::Core)
target_include_directories(paneromultirenametest PRIVATE ${CMAKE_SOURCE_DIR}/app/Dialogs)

# Windows cloud folders (D-020): placeholder detection, badges, pin actions, sync folder discovery.
ecm_add_test(panerocloudfilestest.cpp
    TEST_NAME panerocloudfilestest
    LINK_LIBRARIES Qt5::Test Qt5::Gui PaneroCloudFiles)
target_include_directories(panerocloudfilestest PRIVATE ${CMAKE_SOURCE_DIR}/app/FileSystem)
set_tests_properties(panerocloudfilestest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

ecm_add_test(percentalsplittertest.cpp ../app/Dialogs/percentalsplitter.cpp
    TEST_NAME percentalsplittertest
    LINK_LIBRARIES Qt5::Test Qt5::Widgets)
target_include_directories(percentalsplittertest PRIVATE ${CMAKE_SOURCE_DIR}/app/Dialogs)
