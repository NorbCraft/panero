set(FileSystem_SRCS
    defaultfilesystem.cpp
    dirlisterinterface.cpp
    fileitem.cpp
    filesystem.cpp
    filesystemprovider.cpp
    krpermhandler.cpp
    krquery.cpp
    krtrashhandler.cpp
    sizecalculator.cpp
    virtualfilesystem.cpp
)

# Panero: cloud/network mount helpers, shared with the Cloud library (one
# copy of their state: the revalidation hook is set by PaneroCloud)
add_library(PaneroSlowPaths STATIC paneroslowpaths.cpp panerospecialfolders.cpp)
target_link_libraries(PaneroSlowPaths Qt5::Core Qt5::Concurrent)

# Panero: Windows cloud folders (OneDrive, Google Drive, Dropbox) and their
# Cloud Files placeholders (D-020); pure parts are unit-tested on Linux
add_library(PaneroCloudFiles STATIC panerocloudfiles.cpp)
target_link_libraries(PaneroCloudFiles Qt5::Core Qt5::Gui)
if(WIN32)
    target_link_libraries(PaneroCloudFiles advapi32)
endif()

add_library(FileSystem STATIC ${FileSystem_SRCS})

target_link_libraries(FileSystem
    PaneroSlowPaths
    PaneroCloudFiles
    Qt5::Concurrent
    KF5::I18n
    KF5::KIOCore
    KF5::KIOWidgets
)

if(ACL_FOUND)
    target_link_libraries(FileSystem ${ACL_LIBS})
endif(ACL_FOUND)
