cmake_minimum_required(VERSION 3.21)
project(dkbom VERSION 1.0.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Network Svg Test)
qt_standard_project_setup()

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Reading and writing the schematic is a library of its own: no Quick, no
# network, so the tests can drive it directly.
qt_add_library(dkbom_core STATIC
  src/schematic.cpp
  src/bomcsv.cpp
  src/bommodel.cpp
  src/project.cpp
  src/prefs.cpp
  src/pricing.cpp
)

target_include_directories(dkbom_core PUBLIC src)
target_link_libraries(dkbom_core PUBLIC Qt6::Core)

qt_add_executable(dkbom
  src/main.cpp
  src/api.cpp
  src/catalog.cpp
  src/search.cpp
  src/resultsmodel.cpp
  src/netcache.cpp
  src/ratelimit.cpp
  src/products.cpp
  src/shell.cpp
)

qt_add_resources(dkbom "dkbom_assets"
  PREFIX "/"
  FILES
    assets/Phosphor.ttf
    assets/icons/dkbom.svg
    assets/icons/arrow-clockwise.svg
    assets/icons/arrow-counter-clockwise.svg
    assets/icons/arrow-right.svg
    assets/icons/arrow-u-up-left.svg
    assets/icons/caret-down.svg
    assets/icons/caret-right.svg
    assets/icons/columns.svg
    assets/icons/floppy-disk.svg
    assets/icons/folder-open.svg
    assets/icons/funnel.svg
    assets/icons/funnel-x.svg
    assets/icons/gear.svg
    assets/icons/magnifying-glass.svg
    assets/icons/sliders.svg
    assets/icons/trash.svg
    assets/icons/x.svg
)

qt_add_qml_module(dkbom
  URI Dkbom
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/FilterRail.qml
    qml/ResultsView.qml
    qml/BomView.qml
    qml/PhotoPeek.qml
    qml/PIcon.qml
    qml/Format.js
    qml/Phosphor.js
)

target_include_directories(dkbom PRIVATE src)

target_link_libraries(dkbom PRIVATE
  dkbom_core
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::QuickDialogs2
  Qt6::Network
  Qt6::Svg
)

install(TARGETS dkbom RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/dkbom.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/icons/dkbom.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

configure_file(cmake/uninstall.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake @ONLY)
add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

enable_testing()
add_subdirectory(tests)
