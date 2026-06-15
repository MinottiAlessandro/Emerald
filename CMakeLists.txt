cmake_minimum_required(VERSION 3.21)
project(Emerald VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(emerald
  src/main.cpp
  src/core/Note.h
  src/core/Vault.h
  src/core/Vault.cpp
  src/core/LinkIndex.h
  src/core/LinkIndex.cpp
  src/ui/MainWindow.h
  src/ui/MainWindow.cpp
  src/ui/MarkdownEditor.h
  src/ui/MarkdownEditor.cpp
  src/ui/MarkdownHighlighter.h
  src/ui/MarkdownHighlighter.cpp
  resources/resources.qrc
)

target_include_directories(emerald PRIVATE src)
target_link_libraries(emerald PRIVATE Qt6::Widgets)
