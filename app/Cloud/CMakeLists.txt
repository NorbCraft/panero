# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Panero cloud drives (rclone) and server connections (KIO), see dev/DECISIONS.md D-014.
# PaneroCloud::ensureRclone() uses PaneroPackageInstaller from the panero executable.

set(Cloud_SRCS
    panerocloudutils.cpp
    panerocloud.cpp
    paneroaddclouddialog.cpp
    paneroconnectserverdialog.cpp
)

add_library(Cloud STATIC ${Cloud_SRCS})

target_link_libraries(Cloud
    Qt5::Widgets
    KF5::ConfigCore
    KF5::CoreAddons
    KF5::I18n
    KF5::KIOCore
    KF5::KIOWidgets
    KF5::WidgetsAddons
)
