cmake_minimum_required(VERSION 3.21)

# ── Plugin ──────────────────────────────────────────────
project(OpenRGBBatteryPlugin LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(OpenRGBBatteryPlugin SHARED
    src/BatteryPlugin.cpp
    src/BatteryPlugin.h
    src/SettingsWidget.cpp
    src/SettingsWidget.h
    src/DeathAdderV2ProBatteryProvider.cpp
    src/DeathAdderV2ProBatteryProvider.h
)

target_include_directories(OpenRGBBatteryPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/OpenRGBPluginSDK
)

target_link_libraries(OpenRGBBatteryPlugin PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    setupapi.lib
    hid.lib
)

set_target_properties(OpenRGBBatteryPlugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "OpenRGBBatteryPlugin"
)

if(MSVC)
    target_compile_options(OpenRGBBatteryPlugin PRIVATE
        /utf-8
        /guard:cf
        /sdl
    )
    target_link_options(OpenRGBBatteryPlugin PRIVATE
        /guard:cf
        /CETCOMPAT
        /DYNAMICBASE
        /NXCOMPAT
        /HIGHENTROPYVA
    )
endif()

# ── BatteryProbe (standalone diagnostic tool) ──────────
add_executable(BatteryProbe tools/BatteryProbe/BatteryProbe.cpp)
target_link_libraries(BatteryProbe PRIVATE setupapi.lib hid.lib)

if(MSVC)
    set_property(TARGET BatteryProbe PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded"
    )
endif()

# BatteryProbe has no Qt/QObjects, disable AUTOMOC
set_target_properties(BatteryProbe PROPERTIES
    AUTOMOC OFF
)