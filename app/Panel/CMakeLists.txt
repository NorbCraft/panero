add_subdirectory(PanelView)

set(Panel_SRCS
    panerotheme.cpp
    paneroappearance.cpp
    dirhistoryqueue.cpp
    krcalcspacedialog.cpp
    krcolorcache.cpp
    krerrordisplay.cpp
    krfiletreeview.cpp
    paneropaths.cpp
    paneroportaldrag.cpp
    panerowinshell.cpp
    panerodrivebar.cpp
    paneroiconthemes.cpp
    panerojobrow.cpp
    krlayoutfactory.cpp
    krpanel.cpp
    krpreviewjob.cpp
    krpreviewpopup.cpp
    krpreviews.cpp
    krsearchbar.cpp
    listpanel.cpp
    listpanelactions.cpp
    listpanelframe.cpp
    panelcontextmenu.cpp
    panelfunc.cpp
    sidebar.cpp
    viewactions.cpp
)

add_library(Panel STATIC ${Panel_SRCS})

target_link_libraries(Panel
    Dialogs
    GUI
    KViewer
    KF5::Archive
    KF5::ConfigCore
    KF5::CoreAddons
    KF5::I18n
    KF5::IconThemes
    KF5::KIOFileWidgets
    KF5::KIOWidgets
    KF5::Service
    KF5::Solid
    Qt5::Concurrent
    KF5::WidgetsAddons
    KF5::XmlGui
    Qt5::Xml
    Qt5::DBus
)

target_link_libraries(Panel PaneroCloudFiles)
if(WIN32)
    # Explorer's context menu (D-019): IContextMenu, IShellFolder, window subclassing
    target_sources(Panel PRIVATE panerowinshellmenu.cpp)
    # shell icons: system image list (ImageList_GetIcon)
    target_link_libraries(Panel comctl32 ole32 shell32 uuid)
endif()
