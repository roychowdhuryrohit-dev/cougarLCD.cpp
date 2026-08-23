find_package(PkgConfig REQUIRED)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi-hidraw)

add_executable(cougar-lcd
    main.cpp
    hid_transport.cpp
    renderer.cpp
    telemetry.cpp
)
target_compile_features(cougar-lcd PRIVATE cxx_std_17)
target_compile_options(cougar-lcd PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(cougar-lcd PRIVATE
    PkgConfig::CAIRO
    PkgConfig::FONTCONFIG
    PkgConfig::HIDAPI
    ${CMAKE_DL_LIBS}
)

install(TARGETS cougar-lcd RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_test(NAME render-smoke
        COMMAND cougar-lcd --render-only ${CMAKE_CURRENT_BINARY_DIR}/smoke.png)
endif()

