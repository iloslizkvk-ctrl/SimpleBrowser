cmake_minimum_required(VERSION 3.20)
project(SimpleArchive LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "SimpleArchive is a Windows application.")
endif()

add_executable(SimpleArchive WIN32 src/main.cpp)
target_compile_features(SimpleArchive PRIVATE cxx_std_17)
target_compile_definitions(SimpleArchive PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_link_libraries(SimpleArchive PRIVATE shell32 ole32 comdlg32)

if(MSVC)
  # Link the C/C++ runtime statically so users do not need the VC++ Redistributable.
  set_property(TARGET SimpleArchive PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(SimpleArchive PRIVATE /W4 /permissive- /EHsc)
endif()
