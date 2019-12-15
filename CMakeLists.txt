cmake_minimum_required(VERSION 3.5)

project(goby3-clang LANGUAGES CXX)

execute_process(COMMAND llvm-config --cmakedir OUTPUT_VARIABLE LLVM_CMAKE_DIR)
find_package(LLVM PATHS ${LLVM_CMAKE_DIR})
execute_process(COMMAND llvm-config --includedir OUTPUT_VARIABLE LLVM_INCLUDE_DIR)
include_directories(${LLVM_INCLUDE_DIR})
find_package(yaml-cpp REQUIRED)
find_package(GOBY 3 REQUIRED)

execute_process(COMMAND llvm-config --cxxflags OUTPUT_VARIABLE LLVM_CXX_FLAGS)
string(STRIP ${LLVM_CXX_FLAGS} LLVM_CXX_FLAGS_CLEAN)
execute_process(COMMAND llvm-config --ldflags OUTPUT_VARIABLE LLVM_LD_FLAGS)
string(STRIP ${LLVM_LD_FLAGS} LLVM_LD_FLAGS_CLEAN)
set(CMAKE_CXX_FLAGS "${LLVM_CXX_FLAGS_CLEAN} ${LLVM_LD_FLAGS_CLEAN} -fexceptions")

include_directories(${GOBY_INCLUDE_DIR})

add_executable(goby_clang_tool tool.cpp generate.cpp visualize.cpp)
set_target_properties(goby_clang_tool PROPERTIES COMPILE_FLAGS "${LLVM_CXX_FLAGS_CLEAN} ${LLVM_LD_FLAGS_CLEAN} -fexceptions")

target_link_libraries(goby_clang_tool
  PRIVATE
  clangTooling
  clangFrontend
  clangDriver
  clangSerialization
  clangParse
  clangSema
  clangAnalysis
  clangEdit
  clangAST
  clangASTMatchers
  clangLex
  clangBasic
  LLVM
  goby
  yaml-cpp
  )
