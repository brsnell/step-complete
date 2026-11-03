# Developer tools (not installed): benchmark model generator.
add_executable(sc_make_bench_model make_bench_model.cpp)
# TKFillet only exists for this tool; the app never builds fillets.
target_link_libraries(sc_make_bench_model PRIVATE sc::core TKFillet)
sc_target_defaults(sc_make_bench_model)

add_executable(sc_bench_import bench_import.cpp)
target_link_libraries(sc_bench_import PRIVATE sc::core)
sc_target_defaults(sc_bench_import)
