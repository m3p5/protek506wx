cmake_minimum_required(VERSION 3.16)
project(Protek506Logger VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------
# wxWidgets
# ----------------------------------------------------------------
# Prefer a wx-config-discovered install, then CMake package.
find_package(wxWidgets REQUIRED
    COMPONENTS core base
    OPTIONAL_COMPONENTS adv
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
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.protek506logger")
    set(MACOSX_BUNDLE_SHORT_VERSION  "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

    # App icon (optional — will be silently skipped if file absent)
    set(ICON_FILE "${CMAKE_SOURCE_DIR}/resources/protek506.icns")
    if(EXISTS "${ICON_FILE}")
        list(APPEND SOURCES ${ICON_FILE})
        set_source_files_properties(${ICON_FILE}
            PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        set(MACOSX_BUNDLE_ICON_FILE protek506.icns)
    endif()

    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${SOURCES})

    # IOKit + CoreFoundation for serial port enumeration on macOS
    find_library(IOKIT_LIB   IOKit)
    find_library(COREFOUND_LIB CoreFoundation)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${IOKIT_LIB} ${COREFOUND_LIB})

else()
    # Linux
    add_executable(${PROJECT_NAME} ${SOURCES})
endif()

# ----------------------------------------------------------------
# wxWidgets link
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
