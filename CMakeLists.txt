cmake_minimum_required (VERSION 3.10)
project (bladepsgi)


#
# BladePSGI_Perl library
#

execute_process(COMMAND perl -MConfig -e print\ \$Config{use64bitint} OUTPUT_VARIABLE PERLCONFIG_USE64BITINT RESULT_VARIABLE PERL_FAILED)
if(NOT PERL_FAILED STREQUAL "0")
    message(FATAL_ERROR "Perl not found")
elseif(NOT PERLCONFIG_USE64BITINT STREQUAL "define")
    message(FATAL_ERROR "Your Perl installation has not been configured with use64bitint")
endif()

execute_process(COMMAND perl -MConfig -e print\ \$Config{use64bitall} OUTPUT_VARIABLE PERLCONFIG_USE64BITINT)
if(NOT PERLCONFIG_USE64BITINT STREQUAL "define")
    message(FATAL_ERROR "Your Perl installation has not been configured with use64bitall")
endif()

execute_process(COMMAND perl -MConfig -e print\ \$Config{ivsize} OUTPUT_VARIABLE PERLCONFIG_USE64BITINT)
if(NOT PERLCONFIG_USE64BITINT STREQUAL "8")
    message(FATAL_ERROR "Your Perl installation has not been configured with a 64-bit ivsize")
endif()

execute_process(COMMAND perl -MConfig -e print\ \$Config{ptrsize} OUTPUT_VARIABLE PERLCONFIG_USE64BITINT)
if(NOT PERLCONFIG_USE64BITINT STREQUAL "8")
    message(FATAL_ERROR "Your Perl installation has not been configured with a 64-bit ptrsize")
endif()

add_library(bladepsgi_perl STATIC src/perl/XS.c src/perl/bladepsgi_perl.c src/perl/typemap)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/src/perl/XS.c
    COMMAND xsubpp -typemap typemap src/perl/XS.xs > ${CMAKE_CURRENT_BINARY_DIR}/src/perl/XS.c
    DEPENDS src/perl/XS.xs src/perl/typemap
)

execute_process(COMMAND perl -MExtUtils::Embed -e ccopts OUTPUT_VARIABLE PERLXS_CFLAGS)
string(REGEX REPLACE "\n" "" PERLXS_CFLAGS "${PERLXS_CFLAGS}")
execute_process(COMMAND perl -MExtUtils::Embed -e ldopts OUTPUT_VARIABLE PERLXS_LDFLAGS)
string(REGEX REPLACE "\n" "" PERLXS_LDFLAGS "${PERLXS_LDFLAGS}")
# Remove leading/trailing whitespace as well or CMake will complain; see policy
# CMP0004
string(REGEX REPLACE "(^ +)|( +$)" "" PERLXS_LDFLAGS "${PERLXS_LDFLAGS}")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${PERLXS_CFLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PERLXS_CFLAGS}")


#
# Main binary
#

file(GLOB bladepsgi_SOURCES
    "src/*.hpp"
    "src/*.cpp"
    "src/perl/*.cpp"
)
add_executable(bladepsgi ${bladepsgi_SOURCES})
target_link_libraries(bladepsgi bladepsgi_perl)

# This is horrible, but there doesn't seem to be a better way.  See
# https://cmake.org/pipermail/cmake/2009-February/026790.html.
target_link_libraries(bladepsgi ${PERLXS_LDFLAGS})

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if(COMPILER_SUPPORTS_CXX11)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else()
    message(FATAL_ERROR "Your compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -g")
