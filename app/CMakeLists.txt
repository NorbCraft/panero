configure_file(krusaderversion.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/krusaderversion.h)

add_subdirectory(ActionMan)
add_subdirectory(Archive)
add_subdirectory(BookMan)
if(NOT WIN32)
    # cloud drives: rclone mounts (FUSE), Secret Service keyring, rc unix sockets
    add_subdirectory(Cloud)
endif()
add_subdirectory(Dialogs)
add_subdirectory(DiskUsage)
add_subdirectory(FileSystem)
add_subdirectory(Filter)
add_subdirectory(GUI)
add_subdirectory(Konfigurator)
add_subdirectory(KViewer)
add_subdirectory(JobMan)
add_subdirectory(Locate)
add_subdirectory(MountMan)
add_subdirectory(Panel)
add_subdirectory(QuickView)
add_subdirectory(Search)
add_subdirectory(Splitter)
add_subdirectory(UserAction)
add_subdirectory(resources)

if(SYNCHRONIZER_ENABLED)
    add_subdirectory(Synchronizer)
endif(SYNCHRONIZER_ENABLED)

# Icon themes shipped with Panero (D-020): the folder made by
# dev/scripts/fetch-win11-icons.sh (pinned commit, SHA-256 checked).
set(PANERO_ICON_THEMES_DIR "" CACHE PATH "Verified icon themes to ship with Panero (dev/scripts/fetch-win11-icons.sh <dir> -> <dir>/icons)")
if(PANERO_ICON_THEMES_DIR)
    if(NOT EXISTS "${PANERO_ICON_THEMES_DIR}/Win11/index.theme")
        message(FATAL_ERROR "PANERO_ICON_THEMES_DIR has no Win11 theme: ${PANERO_ICON_THEMES_DIR}")
    endif()
    install(DIRECTORY "${PANERO_ICON_THEMES_DIR}/" DESTINATION ${KDE_INSTALL_DATADIR}/panero/icons USE_SOURCE_PERMISSIONS)
    message(STATUS "Shipping icon themes from ${PANERO_ICON_THEMES_DIR}")
endif()

set(krusader_SRCS
    krglobal.cpp
    icon.cpp
    filelisticon.cpp
    actionsbase.cpp
    tabactions.cpp
    kractions.cpp
    paneltabbar.cpp
    panelmanager.cpp
    krservices.cpp
    main.cpp
    krusaderview.cpp
    krusader.cpp
    krslots.cpp
    krdebuglogger.cpp
    paneroimport.cpp
    paneronavigation.cpp
    paneroviewmodeaction.cpp
    panerotoolbar.cpp
    paneropackageinstaller.cpp
)

qt5_add_resources(krusader_RC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc" )

if(WIN32)
    # Icon and version resource; WIN32_EXECUTABLE = no console window
    configure_file(windows/panero.rc.in ${CMAKE_CURRENT_BINARY_DIR}/panero.rc @ONLY)
    list(APPEND krusader_SRCS ${CMAKE_CURRENT_BINARY_DIR}/panero.rc)
endif()

add_executable(panero ${krusader_SRCS} ${krusader_RC_SRCS})
if(WIN32)
    set_target_properties(panero PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(panero
                      QuickView
                      Panel
                      PanelView
                      BookMan
                      Dialogs
                      DiskUsage
                      GUI
                      Konfigurator
                      KViewer
                      MountMan
                      FileSystem
                      Search
                      Splitter
                      Locate
                      UserAction
                      ActionMan
                      KViewer
                      Filter
                      Dialogs
                      GUI
                      Archive
                      JobMan
                      KF5::Notifications
                      KF5::Parts
                      KF5::WindowSystem
                      KF5::Crash
                      Qt5::PrintSupport
                      Qt5::Concurrent
                      # Panero: static libs (GUI, PanelView) use Panel helpers
                      Panel
)

if(NOT WIN32)
    target_link_libraries(panero Cloud)
endif()

if(SYNCHRONIZER_ENABLED)
    target_link_libraries( panero Synchronizer )
endif(SYNCHRONIZER_ENABLED)

install(TARGETS panero ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS io.github.norbcraft.panero.desktop
        DESTINATION ${KDE_INSTALL_APPDIR} )
install(FILES paneroui.rc
              panerolisterui.rc
              paneroviewer.rc
        DESTINATION ${KDE_INSTALL_KXMLGUI5DIR}/panero)
install(FILES midnight_commander.color
              total_commander.color
              total_commander.keymap
              total_commander.keymap.info
              useraction_examples.xml
              layout.xml
        DESTINATION ${KDE_INSTALL_DATADIR}/panero)
install(FILES io.github.norbcraft.panero.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
# notice when a file operation kept in the background ends (D-022, Linux)
install(FILES panero.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFY5RCDIR})

# Panero application icon (original artwork, see dev/ICON-PROVENANCE.md)
ecm_install_icons(ICONS
    icons/panero/16-apps-io.github.norbcraft.panero.png
    icons/panero/22-apps-io.github.norbcraft.panero.png
    icons/panero/24-apps-io.github.norbcraft.panero.png
    icons/panero/32-apps-io.github.norbcraft.panero.png
    icons/panero/48-apps-io.github.norbcraft.panero.png
    icons/panero/64-apps-io.github.norbcraft.panero.png
    icons/panero/128-apps-io.github.norbcraft.panero.png
    icons/panero/256-apps-io.github.norbcraft.panero.png
    icons/panero/sc-apps-io.github.norbcraft.panero.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor
)

# "Panero Line" icons (Lucide, ISC): a private set Panero layers over the desktop theme
install(DIRECTORY icons/panero-line/light icons/panero-line/dark
        DESTINATION ${KDE_INSTALL_DATADIR}/panero/icons/panero-line
        FILES_MATCHING PATTERN "*.svg")
