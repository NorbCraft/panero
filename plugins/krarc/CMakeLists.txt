set(kio_krarc_PART_SRCS
    krarc.cpp
    krarcbasemanager.cpp
    krlinecountingprocess.cpp
    ../../app/krdebuglogger.cpp
)

# Panero: build as a plain module and install it into Panero's private plugin
# directory (not the global kf5/kio namespace) to avoid colliding with Krusader.
add_library(kio_krarc MODULE ${kio_krarc_PART_SRCS})
set_target_properties(kio_krarc PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/kf5/kio")
install(TARGETS kio_krarc DESTINATION ${PANERO_PRIVATE_PLUGIN_DIR}/kf5/kio)

#this is a library so it needs to enforce it's translation domain, not use the application's domain.
add_definitions(-DTRANSLATION_DOMAIN="panero")

# TODO porting : new variables needed?
target_link_libraries(kio_krarc
    Qt5::Gui
    KF5::Archive
    KF5::ConfigCore
    KF5::CoreAddons
    KF5::I18n
    KF5::KIOCore
)
