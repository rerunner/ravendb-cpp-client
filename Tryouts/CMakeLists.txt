cmake_minimum_required(VERSION 3.14)

set(CMAKE_CXX_STANDARD 17)

add_executable(Raven_CppClient_Tryouts
        pch.cpp
        pch.h
        resource.h
        Tryouts.cpp
        #tryouts1.cpp
        #tryouts2.cpp
        )

add_dependencies(Raven_CppClient_Tryouts Raven_CppClient Raven_CppClient_Tests)

target_include_directories(Raven_CppClient_Tryouts PRIVATE 
	${CLIENT_DIR}
    ${CMAKE_INCLUDE_PATH}
    ${ROOT_INCLUDE_PATH}/nlohmann 
    ${xxhash_cpp_SOURCE_DIR}/xxhash
    ${CURL_INCLUDE_DIRS}
	${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR}
	${CMAKE_CURRENT_BINARY_DIR})


target_link_libraries(Raven_CppClient_Tryouts Raven_CppClient 
    Raven_CppClient 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    ${CURL_LIBRARIES} 
    Threads::Threads)
if(NOT WIN32)
	target_link_libraries(Raven_CppClient_Tryouts stdc++fs)
endif()

find_package(ZLIB REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

find_library(CURL_LIBRARIES curl)

target_link_libraries(Raven_CppClient_Tryouts ZLIB::ZLIB nlohmann_json nlohmann_json::nlohmann_json xxhash OpenSSL::SSL OpenSSL::Crypto ${CURL_LIBRARIES} Threads::Threads)
