cmake_minimum_required(VERSION 3.21)
project(Boonton4200-Control LANGUAGES CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Boonton4200-Control benötigt ein 64-Bit-Windows-Kit.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network LinguistTools)
qt_standard_project_setup()
set(QT_I18N_SOURCE_LANGUAGE de)
set(CMAKE_AUTORCC ON)
option(BOONTON_QT_COMMUNITY "Show About Qt in the community build" ON)

set(QWT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/qwt" CACHE PATH "Qt-6-Qwt-Installation")
find_path(QWT_INCLUDE_DIR NAMES qwt_plot.h HINTS "${QWT_ROOT}" PATH_SUFFIXES include include/qwt REQUIRED)
find_library(QWT_IMPORT_LIBRARY NAMES qwt qwt-qt6 HINTS "${QWT_ROOT}" PATH_SUFFIXES lib REQUIRED)
find_file(QWT_RUNTIME_DLL NAMES qwt.dll qwt-qt6.dll HINTS "${QWT_ROOT}" PATH_SUFFIXES bin lib REQUIRED)
add_library(Qwt::Qwt SHARED IMPORTED)
set_target_properties(Qwt::Qwt PROPERTIES
    IMPORTED_IMPLIB "${QWT_IMPORT_LIBRARY}"
    IMPORTED_LOCATION "${QWT_RUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${QWT_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS QWT_DLL
)

qt_add_executable(Boonton4200-Control WIN32
    main.cpp version.h
    mainwindow.cpp mainwindow.h updates.cpp
    boonton4200.cpp boonton4200.h
    gpib.cpp gpib.h
    assets/app.rc assets/app.qrc
)
target_include_directories(Boonton4200-Control PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_definitions(Boonton4200-Control PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Boonton4200-Control PRIVATE Qt6::Widgets Qt6::Network Qwt::Qwt)
if(BOONTON_QT_COMMUNITY)
    target_compile_definitions(Boonton4200-Control PRIVATE BOONTON_QT_COMMUNITY)
endif()
if(MSVC)
    target_compile_options(Boonton4200-Control PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
endif()

qt_add_translations(Boonton4200-Control TS_FILES translations/boonton4200_en.ts translations/boonton4200_fr.ts)
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

add_custom_command(TARGET Boonton4200-Control POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qwt::Qwt>" "$<TARGET_FILE_DIR:Boonton4200-Control>"
    VERBATIM
)
