# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Panero cloud drives (rclone) and server connections (KIO), see dev/DECISIONS.md
# D-014 and the security design in dev/CLOUD-SECURITY-AUDIT.md (D-016).
# PaneroCloud::ensureRclone() uses PaneroPackageInstaller from the panero executable.

set(PANERO_LIBEXEC_DIR "${KDE_INSTALL_FULL_LIBEXECDIR}/panero")

set(Cloud_SRCS
    panerocloudutils.cpp
    panerosecretstore.cpp
    panerorclone.cpp
    panerocloudrevoke.cpp
    panerocloudsetup.cpp
    panerooffline.cpp
    panerocloud.cpp
    paneroaddclouddialog.cpp
    paneroofflinedialog.cpp
    paneroconnectserverdialog.cpp
)

# provider icons (Google Drive, OneDrive): own simple drawings, compiled in
qt5_add_resources(Cloud_SRCS icons/cloudicons.qrc)

add_library(Cloud STATIC ${Cloud_SRCS})
target_compile_definitions(Cloud PRIVATE PANERO_LIBEXEC_DIR="${PANERO_LIBEXEC_DIR}")

target_link_libraries(Cloud
    Qt5::Widgets
    Qt5::DBus
    Qt5::Network
    KF5::ConfigCore
    KF5::CoreAddons
    KF5::I18n
    KF5::KIOCore
    KF5::KIOWidgets
    KF5::WidgetsAddons
)

# rclone's --password-command: reads the configuration key from the keyring
add_executable(panero-cloud-key panerocloudkey.cpp panerosecretstore.cpp)
target_link_libraries(panero-cloud-key Qt5::Core Qt5::DBus)
install(TARGETS panero-cloud-key DESTINATION ${KDE_INSTALL_LIBEXECDIR}/panero)

# Panero's own rclone (R1): a verified upstream binary, fetched before the
# build by dev/scripts/fetch-rclone.sh (pinned version and SHA-256 in
# dev/rclone/). Copied next to the executables for runs from the build tree.
set(PANERO_RCLONE_BINARY "" CACHE FILEPATH "Verified rclone binary to ship with Panero (see dev/rclone/README.md)")
if(PANERO_RCLONE_BINARY)
    if(NOT EXISTS "${PANERO_RCLONE_BINARY}")
        message(FATAL_ERROR "PANERO_RCLONE_BINARY does not exist: ${PANERO_RCLONE_BINARY}")
    endif()
    if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
        set(_panero_bin "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
    else()
        set(_panero_bin "${CMAKE_BINARY_DIR}/bin")
    endif()
    configure_file("${PANERO_RCLONE_BINARY}" "${_panero_bin}/panero-rclone" COPYONLY)
    install(PROGRAMS "${PANERO_RCLONE_BINARY}" DESTINATION ${KDE_INSTALL_LIBEXECDIR}/panero RENAME rclone)
    message(STATUS "Shipping rclone from ${PANERO_RCLONE_BINARY}")
else()
    message(STATUS "PANERO_RCLONE_BINARY not set: cloud drives need PANERO_RCLONE at runtime")
endif()
