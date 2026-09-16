cmake_minimum_required(VERSION 3.16)
project(Genesis VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Pdf PdfWidgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/titlebar/TitleBar.cpp
    src/titlebar/WindowButton.cpp
    src/titlebar/PinButton.cpp
    src/titlebar/TitleMenuItem.cpp
    src/dialogs/FramelessDialog.cpp
    src/dialogs/BookmarkDialog.cpp
    src/dialogs/ExcerptDialog.cpp
    src/dialogs/MdEditorDialog.cpp
    src/library/Library.cpp
    src/library/BookInfo.cpp
    src/library/MetadataStore.cpp
    src/library/BookmarkStore.cpp
    src/library/ReadingLogStore.cpp
    src/library/ExcerptStore.cpp
    src/settings/Theme.cpp
    src/settings/BackgroundWidget.cpp
    src/settings/BackgroundDialog.cpp
    src/bookshelf/IconButton.cpp
    src/bookshelf/BookCardWidget.cpp
    src/bookshelf/CardGrid.cpp
    src/bookshelf/BookshelfView.cpp
    src/bookshelf/DurationFormat.cpp
    src/reader/ReaderView.cpp
    src/reader/DoublePageView.cpp
    src/reader/NotesPanel.cpp
    src/reader/ExcerptView.cpp
    src/stats/ReadingStatsPanel.cpp
    src/stats/SegmentedToggle.cpp
    src/stats/StatTile.cpp
    src/stats/TrendChartWidget.cpp
    src/stats/HourPieWidget.cpp
    src/widgets/MenuOption.cpp
)

set(HEADERS
    src/MainWindow.h
    src/titlebar/TitleBar.h
    src/titlebar/WindowButton.h
    src/titlebar/PinButton.h
    src/titlebar/TitleMenuItem.h
    src/dialogs/FramelessDialog.h
    src/dialogs/BookmarkDialog.h
    src/dialogs/ExcerptDialog.h
    src/dialogs/MdEditorDialog.h
    src/library/Library.h
    src/library/BookInfo.h
    src/library/MetadataStore.h
    src/library/BookmarkStore.h
    src/library/ReadingLogStore.h
    src/library/ExcerptStore.h
    src/settings/Theme.h
    src/settings/BackgroundWidget.h
    src/settings/BackgroundDialog.h
    src/bookshelf/IconButton.h
    src/bookshelf/BookCardWidget.h
    src/bookshelf/CardGrid.h
    src/bookshelf/BookshelfView.h
    src/bookshelf/DurationFormat.h
    src/reader/ReaderView.h
    src/reader/DoublePageView.h
    src/reader/NotesPanel.h
    src/reader/ExcerptView.h
    src/stats/ReadingStatsPanel.h
    src/stats/SegmentedToggle.h
    src/stats/StatTile.h
    src/stats/TrendChartWidget.h
    src/stats/HourPieWidget.h
    src/widgets/MenuOption.h
)

# Embedded Qt resources (runtime window/taskbar icon).
set(RESOURCES
    resources.qrc
)

add_executable(Genesis WIN32 ${SOURCES} ${HEADERS} ${RESOURCES})

# Windows executable icon (Explorer / taskbar / Task Manager). Compiled in a
# standalone OBJECT library so windres is not handed Qt's include dirs — those
# contain a space ("My Documents") that windres' preprocessor cannot handle.
if(WIN32)
    set(GENESIS_ICON_PATH "${CMAKE_SOURCE_DIR}/picture/genesis.ico")
    configure_file(
        "${CMAKE_SOURCE_DIR}/src/resources/app_icon.rc.in"
        "${CMAKE_BINARY_DIR}/app_icon.rc"
        @ONLY
    )
    add_library(genesis_icon OBJECT "${CMAKE_BINARY_DIR}/app_icon.rc")
    # configure_file() records no dependency on the .ico it points at, so ninja
    # would keep an exe built from a stale icon after picture/genesis.ico is
    # regenerated. Declare it explicitly, or a plain rebuild silently keeps the
    # old artwork embedded in the executable.
    set_property(SOURCE "${CMAKE_BINARY_DIR}/app_icon.rc"
                 APPEND PROPERTY OBJECT_DEPENDS "${GENESIS_ICON_PATH}")
    # No moc/uic/rcc for a resource-only target: keeps windres away from the
    # spaced autogen include directory.
    set_target_properties(genesis_icon PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF
    )
    target_link_libraries(Genesis PRIVATE genesis_icon)
endif()

target_link_libraries(Genesis PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Pdf Qt6::PdfWidgets)

# UTF-8 encoding for MSVC
if(MSVC)
    target_compile_options(Genesis PRIVATE /utf-8)
endif()

# Output to build/ directory
set_target_properties(Genesis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
