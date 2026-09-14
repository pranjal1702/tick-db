cmake_minimum_required(VERSION 3.20)
project(tick_db CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)
pkg_check_modules(ARROW REQUIRED arrow)
pkg_check_modules(PARQUET REQUIRED parquet)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ARROW_INCLUDE_DIRS}
    ${PARQUET_INCLUDE_DIRS}
)

add_library(tick_db_lib
    src/schemas/trade.cpp
    src/schemas/quote.cpp
    src/schemas/book_level.cpp
    src/schemas/order_event.cpp
    src/storage/parquet_reader.cpp
    src/storage/parquet_writer.cpp
    src/spatial/rtree.cpp
    src/spatial/sidecar.cpp
    src/query/query_engine.cpp
    src/clustering/compactor.cpp
    src/catalog/catalog.cpp
)

target_link_libraries(tick_db_lib
    PUBLIC
    ${ARROW_LIBRARIES}
    ${PARQUET_LIBRARIES}
)

add_executable(tick_db_ingest
    src/main.cpp
)

target_link_libraries(tick_db_ingest
    PRIVATE
    tick_db_lib
)

add_executable(tick_db_gen
    src/test_gen.cpp
)

target_link_libraries(tick_db_gen
    PRIVATE
    ${ARROW_LIBRARIES}
    ${PARQUET_LIBRARIES}
)

add_executable(tick_db_test
    tests/test_suite.cpp
)

target_link_libraries(tick_db_test
    PRIVATE
    tick_db_lib
)
