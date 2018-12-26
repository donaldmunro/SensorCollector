cmake_minimum_required(VERSION 3.4.1)

add_library(native-lib SHARED native-lib.h native-lib.cc latch.hpp android-logging.h)

find_library(log-lib log)

target_link_libraries(native-lib android ${log-lib} )
