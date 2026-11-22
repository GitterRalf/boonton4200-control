cmake_minimum_required(VERSION 3.21)
project(Boonton4200-Control LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "Boonton4200-Control benötigt ein Windows-Kit.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(DEVICE_QT_MAJOR 6 CACHE STRING "Qt major version (5 or 6)")
set_property(CACHE DEVICE_QT_MAJOR PROPERTY STRINGS 5 6)
if(DEVICE_QT_MAJOR STREQUAL "5")
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Network LinguistTools)
    set(CMAKE_AUTOMOC ON)
elseif(DEVICE_QT_MAJOR STREQUAL "6")
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network LinguistTools)
    qt_standard_project_setup()
else()
    message(FATAL_ERROR "DEVICE_QT_MAJOR must be 5 or 6.")
endif()
set(qt "Qt${DEVICE_QT_MAJOR}")
set(QT_I18N_SOURCE_LANGUAGE de)
set(CMAKE_AUTORCC ON)
option(BOONTON_QT_COMMUNITY "Show About Qt in the community build" ON)

if(DEVICE_QT_MAJOR STREQUAL "5")
    set(qwt_dir qwt-qt5-x86)
else()
    set(qwt_dir qwt)
endif()
set(QWT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/${qwt_dir}" CACHE PATH "Qwt built for the selected Qt kit")
find_path(QWT_INCLUDE_DIR NAMES qwt_plot.h HINTS "${QWT_ROOT}" PATH_SUFFIXES include include/qwt REQUIRED)
find_library(QWT_IMPORT_LIBRARY NAMES qwt qwt-qt${DEVICE_QT_MAJOR} HINTS "${QWT_ROOT}" PATH_SUFFIXES lib REQUIRED)
find_file(QWT_RUNTIME_DLL NAMES qwt.dll qwt-qt${DEVICE_QT_MAJOR}.dll HINTS "${QWT_ROOT}" PATH_SUFFIXES bin lib REQUIRED)
add_library(Qwt::Qwt SHARED IMPORTED)
set_target_properties(Qwt::Qwt PROPERTIES
    IMPORTED_IMPLIB "${QWT_IMPORT_LIBRARY}"
    IMPORTED_LOCATION "${QWT_RUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${QWT_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS QWT_DLL
)

if(DEVICE_QT_MAJOR STREQUAL "5")
    add_executable(Boonton4200-Control WIN32)
else()
    qt_add_executable(Boonton4200-Control WIN32)
endif()
target_sources(Boonton4200-Control PRIVATE
    main.cpp version.h
    mainwindow.cpp mainwindow.h updates.cpp
    boonton4200.cpp boonton4200.h
    gpib.cpp gpib.h
    assets/app.rc assets/app.qrc
)
target_include_directories(Boonton4200-Control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_definitions(Boonton4200-Control PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Boonton4200-Control PRIVATE ${qt}::Widgets ${qt}::Network Qwt::Qwt)
if(BOONTON_QT_COMMUNITY)
    target_compile_definitions(Boonton4200-Control PRIVATE BOONTON_QT_COMMUNITY)
endif()
if(MSVC)
    target_compile_options(Boonton4200-Control PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

set(ts_files translations/boonton4200_en.ts translations/boonton4200_fr.ts)
if(DEVICE_QT_MAJOR STREQUAL "5")
    qt5_add_translation(qm_files ${ts_files})
    set(translation_resources "<RCC><qresource prefix=\"/i18n\">\n")
    foreach(qm IN LISTS qm_files)
        get_filename_component(filename "${qm}" NAME)
        string(APPEND translation_resources "  <file alias=\"${filename}\">${qm}</file>\n")
    endforeach()
    string(APPEND translation_resources "</qresource><qresource prefix=\"/qt-i18n\">\n")
    foreach(locale IN ITEMS de en fr)
        get_filename_component(qt_catalog "${Qt5_DIR}/../../../translations/qtbase_${locale}.qm" ABSOLUTE)
        if(EXISTS "${qt_catalog}")
            string(APPEND translation_resources "  <file alias=\"qtbase_${locale}.qm\">${qt_catalog}</file>\n")
        endif()
    endforeach()
    string(APPEND translation_resources "</qresource></RCC>\n")
    set(translations_qrc "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
    file(CONFIGURE OUTPUT "${translations_qrc}" CONTENT "${translation_resources}" @ONLY)
    qt5_add_resources(translation_sources "${translations_qrc}")
    target_sources(Boonton4200-Control PRIVATE ${translation_sources})
else()
    qt_add_translations(Boonton4200-Control TS_FILES ${ts_files})
    foreach(locale IN ITEMS de en fr)
        set(qt_catalog "${Qt6_DIR}/../../../translations/qtbase_${locale}.qm")
        if(EXISTS "${qt_catalog}")
            get_filename_component(qt_catalog "${qt_catalog}" ABSOLUTE)
            set_source_files_properties("${qt_catalog}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${locale}.qm")
            qt_add_resources(Boonton4200-Control "qt_i18n_${locale}"
                PREFIX "/qt-i18n"
                FILES "${qt_catalog}"
            )
        endif()
    endforeach()
endif()

add_custom_command(TARGET Boonton4200-Control POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qwt::Qwt>" "$<TARGET_FILE_DIR:Boonton4200-Control>"
    VERBATIM
)
