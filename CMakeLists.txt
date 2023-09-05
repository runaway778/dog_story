cmake_minimum_required(VERSION 3.11)

project(game_server CXX)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup(TARGETS)

find_package(Boost 1.78.0 REQUIRED)
if(Boost_FOUND)
  include_directories(${Boost_INCLUDE_DIRS})
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include(CTest)
include(${CONAN_BUILD_DIRS_CATCH2}/Catch.cmake)

add_compile_definitions(BOOST_BEAST_USE_STD_STRING_VIEW)

add_library(loot_genererating_and_collision_detecting_lib STATIC
	src/loot_generator.h
    src/loot_generator.cpp
	src/collision_detector.h
	src/collision_detector.cpp
	src/geom.h
)

target_include_directories(loot_genererating_and_collision_detecting_lib PUBLIC CONAN_PKG::boost)
target_link_libraries(loot_genererating_and_collision_detecting_lib PUBLIC CONAN_PKG::catch2 CONAN_PKG::boost Threads::Threads) 

add_executable(game_server
	src/main.cpp
	src/http_server.cpp
	src/http_server.h
	src/sdk.h
	src/model.h
	src/model.cpp
	src/tagged.h
	src/boost_json.cpp
	src/json_loader.h
	src/json_loader.cpp
	src/request_handler.cpp
	src/request_handler.h
	src/json_encoder.h
	src/json_encoder.cpp
	src/logger.h
	src/ticker.h
	src/tagged_uuid.h
	src/tagged_uuid.cpp
)

add_executable(game_server_tests
    tests/loot_generator_tests.cpp
	tests/collision-detector-tests.cpp
)

catch_discover_tests(game_server_tests)

target_link_libraries(game_server loot_genererating_and_collision_detecting_lib CONAN_PKG::libpq CONAN_PKG::libpqxx)
target_link_libraries(game_server_tests CONAN_PKG::catch2 loot_genererating_and_collision_detecting_lib) 
