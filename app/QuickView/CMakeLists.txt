# SPDX-FileCopyrightText: 2026 Panero contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# Quick View (D-022): the other panel as a viewer of the current item.

# file reading and parsing (worker threads, unit-tested)
add_library(QuickViewData STATIC paneroquickviewdata.cpp)
target_link_libraries(QuickViewData PUBLIC Qt5::Core Qt5::Gui KF5::Archive KF5::Codecs)
if(PANERO_HAVE_POPPLER)
    target_link_libraries(QuickViewData PRIVATE Poppler::Qt5)
    target_compile_definitions(QuickViewData PRIVATE PANERO_HAVE_POPPLER)
endif()

add_library(QuickView STATIC
    paneroquickview.cpp
    paneroquickviewwidgets.cpp
    paneroquickviewcontroller.cpp
)
target_link_libraries(QuickView
    QuickViewData
    PaneroSlowPaths
    PaneroCloudFiles
    Panel
    Qt5::Widgets
    Qt5::Concurrent
    KF5::CoreAddons
    KF5::I18n
    KF5::KIOCore
    KF5::WidgetsAddons
    KF5::XmlGui
)
if(KF5SyntaxHighlighting_FOUND)
    target_link_libraries(QuickView KF5::SyntaxHighlighting)
    target_compile_definitions(QuickView PRIVATE PANERO_HAVE_SYNTAX)
endif()
if(PANERO_HAVE_MULTIMEDIA)
    target_link_libraries(QuickView Qt5::Multimedia Qt5::MultimediaWidgets)
    target_compile_definitions(QuickView PRIVATE PANERO_HAVE_MULTIMEDIA)
endif()
