if(NOT TARGET Ebur128::Ebur128)
    find_package(Ebur128 QUIET)
endif()

if(TARGET Ebur128::Ebur128)
    set(HAVE_EBUR128 TRUE)
else()
    message(STATUS "Ebur128 not found; ebur128 scanner will not be available.")
endif()

create_fooyin_plugin_internal(
    rgscanner
    DEPENDS Fooyin::CorePrivate
            Fooyin::Gui
            ${FFMPEG_LIBRARIES}
    SOURCES ffmpegscanner.cpp
            ffmpegscanner.h
            rgscanner.cpp
            rgscanner.h
            rgscanresults.cpp
            rgscanresults.h
            rgscanresultsmodel.cpp
            rgscanresultsmodel.h
            rgscannerpage.cpp
            rgscannerpage.h
            rgscannerplugin.cpp
            rgscannerplugin.h
            rgscannerdefs.h
            rgscanmemorycache.cpp
            rgscanmemorycache.h
            rgscanmemorycache.cpp
            rgscanmemorycache.h
)

target_include_directories(rgscanner PRIVATE ${FFMPEG_INCLUDE_DIRS})

if(HAVE_EBUR128)
    target_link_libraries(rgscanner PRIVATE Ebur128::Ebur128)
    target_sources(rgscanner PRIVATE ebur128scanner.cpp ebur128scanner.h)
    target_compile_definitions(rgscanner PRIVATE HAVE_EBUR128)
endif()
