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
add_library(PaneroSlowPaths STATIC paneroslowpaths.cpp)
target_link_libraries(PaneroSlowPaths Qt5::Core Qt5::Concurrent)

add_library(FileSystem STATIC ${FileSystem_SRCS})

target_link_libraries(FileSystem
    PaneroSlowPaths
    Qt5::Concurrent
    KF5::I18n
    KF5::KIOCore
    KF5::KIOWidgets
)

if(ACL_FOUND)
    target_link_libraries(FileSystem ${ACL_LIBS})
endif(ACL_FOUND)
