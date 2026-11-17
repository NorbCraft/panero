configure_file(krusaderversion.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/krusaderversion.h)

add_subdirectory(ActionMan)
add_subdirectory(Archive)
add_subdirectory(BookMan)
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
add_subdirectory(Search)
add_subdirectory(Splitter)
add_subdirectory(UserAction)
add_subdirectory(resources)

if(SYNCHRONIZER_ENABLED)
    add_subdirectory(Synchronizer)
endif(SYNCHRONIZER_ENABLED)

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
)

qt5_add_resources(krusader_RC_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/resources.qrc" )

add_executable(panero ${krusader_SRCS} ${krusader_RC_SRCS})

target_link_libraries(panero
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
                      Qt5::PrintSupport
                      Qt5::Concurrent
)

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
