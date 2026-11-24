set(JobMan_SRCS
    jobman.cpp
    krjob.cpp
    panerojobwindow.cpp
    panerorecyclebin.cpp
    paneroundo.cpp)

add_library(JobMan STATIC ${JobMan_SRCS})

target_link_libraries(JobMan
    KF5::ConfigCore
    KF5::CoreAddons
    KF5::I18n
    KF5::KIOWidgets
    KF5::Notifications
    KF5::WidgetsAddons
)
if(WIN32)
    # Recycle Bin: current user's SID (advapi32), SHChangeNotify (shell32)
    target_link_libraries(JobMan advapi32 shell32)
endif()
