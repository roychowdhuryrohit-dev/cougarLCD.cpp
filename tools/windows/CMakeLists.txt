add_executable(cougar-hid-probe cougar_hid_probe.cpp)
target_compile_features(cougar-hid-probe PRIVATE cxx_std_17)
target_compile_definitions(cougar-hid-probe PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
if(MSVC)
    target_compile_options(cougar-hid-probe PRIVATE /W4 /permissive- /utf-8)
endif()
target_link_libraries(cougar-hid-probe PRIVATE setupapi hid)

