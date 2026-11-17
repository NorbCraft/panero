include(FindZLIB)

include(libisofs/CMakeLists.txt)

set(kio_iso_PART_SRCS
    kisodirectory.cpp
    kisofile.cpp
    qfilehack.cpp
    kiso.cpp
    iso.cpp
    ../../app/krdebuglogger.cpp
)

# Panero: build as a plain module and install it into Panero's private plugin
# directory (not the global kf5/kio namespace) to avoid colliding with Krusader.
add_library(kio_iso MODULE ${kio_iso_PART_SRCS} ${libisofs_SRCS})
set_target_properties(kio_iso PROPERTIES PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/kf5/kio")
install(TARGETS kio_iso DESTINATION ${PANERO_PRIVATE_PLUGIN_DIR}/kf5/kio)

#this is a library so it needs to enforce it's translation domain, not use the application's domain.
add_definitions(-DTRANSLATION_DOMAIN="panero")

target_link_libraries(kio_iso
    KF5::Archive
    KF5::ConfigCore
    KF5::KIOCore
    ${ZLIB_LIBRARY}
)

install(FILES panero_kio_isorc DESTINATION ${KDE_INSTALL_CONFDIR})
