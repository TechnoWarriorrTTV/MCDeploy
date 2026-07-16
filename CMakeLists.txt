cmake_minimum_required(VERSION 3.16)
project(MCDeploy CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Configure compiler flags
if(MSVC)
    add_compile_options(/W4 /WX- /EHsc /std:c++20 /utf-8)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
else()
    add_compile_options(-Wall -Wextra -pthread)
endif()

# Find dependencies
find_package(Drogon REQUIRED)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Try to find optional headers/libraries if installed
find_package(spdlog QUIET)

# Include paths
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/MCDeploy/src
    ${CURL_INCLUDE_DIRS}
    ${SQLITE3_INCLUDE_DIRS}
)

# Source files list
set(SOURCES
    MCDeploy/src/main.cpp
    MCDeploy/src/models/Database.cpp
    MCDeploy/src/utils/AppPaths.cpp
    MCDeploy/src/utils/ProcessManager.cpp
    MCDeploy/src/utils/SystemInfo.cpp
    MCDeploy/src/utils/VersionFetcher.cpp
    MCDeploy/src/utils/DnsManager.cpp
    MCDeploy/src/utils/TunnelManager.cpp
    MCDeploy/src/controllers/DashboardController.cpp
    MCDeploy/src/controllers/ServerController.cpp
    MCDeploy/src/controllers/AIEditorController.cpp
    MCDeploy/src/controllers/MembersController.cpp
    MCDeploy/src/controllers/ScheduleController.cpp
    MCDeploy/src/controllers/AnalyticsController.cpp
    MCDeploy/src/controllers/WebPanelController.cpp
    MCDeploy/src/ai/AIAgent.cpp
    MCDeploy/src/utils/BrowserWindow.cpp
    MCDeploy/src/utils/ServerLifecycle.cpp
    MCDeploy/src/utils/Scheduler.cpp
)

# On Windows, embed our .rc file (icon + version info) into the executable.
if(WIN32)
    list(APPEND SOURCES MCDeploy/resources/mcdeploy.rc)
endif()

# Output binary named mcdeploy
add_executable(mcdeploy ${SOURCES})

if(WIN32)
    set_target_properties(mcdeploy PROPERTIES 
        WIN32_EXECUTABLE TRUE
        LINK_FLAGS "/ENTRY:mainCRTStartup"
    )
endif()

# Target Link Libraries
target_link_libraries(mcdeploy
    PRIVATE
    Drogon::Drogon
    ${CURL_LIBRARIES}
    ${SQLITE3_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Windows-specific link libraries + mandatory native WebView2 host window
if(WIN32)
    target_link_libraries(mcdeploy PRIVATE shell32 ole32 user32 gdi32 shcore)

    # The Windows product always opens in its own WebView2 window.
    find_package(unofficial-webview2 CONFIG REQUIRED)
    target_link_libraries(mcdeploy PRIVATE unofficial::webview2::webview2)

    find_path(WIL_INCLUDE_DIR wil/com.h REQUIRED)
    target_include_directories(mcdeploy PRIVATE ${WIL_INCLUDE_DIR})
    message(STATUS "MCDeploy: native WebView2 window dependencies enabled")
endif()

# If spdlog was found, link it
if(spdlog_FOUND)
    target_link_libraries(mcdeploy PRIVATE spdlog::spdlog)
endif()

# Custom build action to print success
add_custom_command(TARGET mcdeploy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "MCDeploy successfully built!"
)
