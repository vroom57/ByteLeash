# Watch-only ETW prototype; not part of the shipped app.
add_executable(byteleash-probe
    main.cpp
    etw_session.cpp
    process_names.cpp
)
target_compile_definitions(byteleash-probe PRIVATE UNICODE _UNICODE)
target_link_libraries(byteleash-probe PRIVATE
    byteleash::core
    byteleash_options
    advapi32
    tdh
    ws2_32
)
