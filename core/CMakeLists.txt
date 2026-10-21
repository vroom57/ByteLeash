add_library(byteleash_core STATIC
    src/app_budget.cpp
    src/engine.cpp
)
add_library(byteleash::core ALIAS byteleash_core)

target_include_directories(byteleash_core PUBLIC include)
target_link_libraries(byteleash_core PRIVATE byteleash_options)
