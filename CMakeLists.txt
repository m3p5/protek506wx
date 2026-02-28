cmake_minimum_required(VERSION 3.16)

# ----------------------------------------------------------------
# Guard against in-source builds
# ----------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed.\n"
        "Create a separate build directory and run cmake from there:\n"
        "  mkdir build && cd build && cmake ..")
endif()

# ----------------------------------------------------------------
# macOS deployment target — must be set BEFORE project().
# ----------------------------------------------------------------
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
        "Minimum macOS deployment version")
endif()

# Version kept in sync with APP_VERSION in MainFrame.cpp
project(Protek506Logger VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------
# wxWidgets
#
# 'adv' is REQUIRED (not optional): wxListCtrl and wxAboutBox live
# there on many wxWidgets package configurations.  Leaving it as
# OPTIONAL_COMPONENTS causes silent build success followed by linker
# errors at link time.
# ----------------------------------------------------------------
find_package(wxWidgets REQUIRED
    COMPONENTS core base adv
)
include(${wxWidgets_USE_FILE})

# ----------------------------------------------------------------
# Sources
# ----------------------------------------------------------------
set(SOURCES
    src/App.cpp
    src/MainFrame.cpp
    src/ReaderThread.cpp
    src/DmmParser.cpp
    src/CsvLogger.cpp
    src/SerialPort.cpp
)

# ----------------------------------------------------------------
# Platform-specific extras
# ----------------------------------------------------------------
if(WIN32)
    # Windows resource file for icon + version info
    list(APPEND SOURCES resources/win32.rc)

    # WIN32 = no console window (GUI subsystem)
    add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

    # setupapi for serial port enumeration
    target_link_libraries(${PROJECT_NAME} PRIVATE setupapi)

elseif(APPLE)
    # macOS app bundle
    set(MACOSX_BUNDLE_BUNDLE_NAME    "Protek 506 Logger")
    # Avoid the reserved com.example domain
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.opensource.protek506logger")
    set(MACOSX_BUNDLE_SHORT_VERSION  "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # App icon (optional — silently skipped if file absent)
    set(ICON_FILE "${CMAKE_SOURCE_DIR}/resources/protek506.ico")
    if(EXISTS "${ICON_FILE}")
        list(APPEND SOURCES ${ICON_FILE})
        set_source_files_properties(${ICON_FILE}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        set(MACOSX_BUNDLE_ICON_FILE protek506.ico)
    endif()

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})

    # IOKit + CoreFoundation for serial port enumeration on macOS
    find_library(IOKIT_LIB     IOKit)
    find_library(COREFOUND_LIB CoreFoundation)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${IOKIT_LIB} ${COREFOUND_LIB})

else()
    # Linux
    # Explicitly link pthreads: wxThread uses it and some wxWidgets
    # package configurations do not pull it in transitively, causing
    # undefined references to pthread_create at link time.
    find_package(Threads REQUIRED)
    add_executable(${PROJECT_NAME} ${SOURCES})
    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads)
endif()

# ----------------------------------------------------------------
# wxWidgets link (all platforms)
# ----------------------------------------------------------------
target_link_libraries(${PROJECT_NAME} PRIVATE ${wxWidgets_LIBRARIES})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${wxWidgets_INCLUDE_DIRS}
    src
)

# ----------------------------------------------------------------
# Compiler warnings / definitions
# ----------------------------------------------------------------
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3 /permissive-)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
else()
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
endif()

# ----------------------------------------------------------------
# Install (optional)
# ----------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    BUNDLE  DESTINATION .
)
