cmake_minimum_required(VERSION 3.5)

project(goby3-clang LANGUAGES CXX)

find_package(LLVM PATHS /usr/lib/llvm-6.0/lib/cmake/llvm)
#find_package(Clang PATHS /usr/share/llvm-6.0/cmake)
include_directories(/usr/lib/llvm-6.0/include)
#set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS "-I/usr/lib/llvm-6.0/include -std=c++0x -fuse-ld=gold -Wl,--no-keep-files-mapped -Wl,--no-map-whole-files -fPIC -fvisibility-inlines-hidden -Werror=date-time -std=c++11 -Wall -W -Wno-unused-parameter -Wwrite-strings -Wcast-qual -Wno-missing-field-initializers -pedantic -Wno-long-long -Wno-maybe-uninitialized -Wdelete-non-virtual-dtor -Wno-comment -ffunction-sections -fdata-sections -O2 -DNDEBUG  -fno-exceptions -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -L/usr/lib/llvm-6.0/lib")

add_executable(goby3-clang-tool tool.cpp)
target_link_libraries(goby3-clang-tool
  PRIVATE
  clangTooling
#  clangFrontendTool
  clangFrontend
  clangDriver
  clangSerialization
#  clangCodeGen
  clangParse
  clangSema
#  clangStaticAnalyzerFrontend
#  clangStaticAnalyzerCheckers
#  clangStaticAnalyzerCore
  clangAnalysis
#  clangARCMigrate
#  clangRewrite
#  clangRewriteFrontend
  clangEdit
  clangAST
  clangASTMatchers
  clangLex
  clangBasic
#  clang
  LLVM
  )
