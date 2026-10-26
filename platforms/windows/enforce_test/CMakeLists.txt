# Driverless enforcement test (docs/plan.md, M1); not part of the shipped app.
add_executable(byteleash-enforce-test
    main.cpp
    flow.cpp
    qos.cpp
    wfp.cpp
)
target_compile_definitions(byteleash-enforce-test PRIVATE UNICODE _UNICODE)
target_link_libraries(byteleash-enforce-test PRIVATE
    byteleash_options
    advapi32
    fwpuclnt
    ws2_32
)
