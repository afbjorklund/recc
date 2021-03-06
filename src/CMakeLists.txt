set(BINARY ${CMAKE_PROJECT_NAME})

include_directories(.)

FILE(GLOB SRCS *.cpp)

if(CMAKE_BUILD_TYPE STREQUAL "DEBUG")
    set(DEBUG_FLAGS -Werror -Wextra -pedantic-errors -Wall -Wconversion -Wno-vla)
endif()

add_library(remoteexecution STATIC ${SRCS})
target_link_libraries(remoteexecution
    ${_EXTRA_LDD_FLAGS}
    Buildbox::buildboxcommon
    reccproto
    ${PROTOBUF_TARGET}
    ${OPENSSL_TARGET}
    ${GRPC_TARGET}
    ${STATIC_GRPC_LINKER_RULE}
    ${ZLIB_LIBRARIES}
    ${OS_LIBS}
)

if (CMAKE_SYSTEM_NAME MATCHES "SunOS")
    target_link_libraries(remoteexecution socket nsl)
endif ()

# recc
add_executable(${BINARY} bin/${BINARY}.m.cpp)
target_link_libraries(${BINARY} remoteexecution)

# casupload
add_executable(casupload bin/casupload.m.cpp)
target_link_libraries(casupload remoteexecution)

# deps
add_executable(deps deps.cpp bin/deps.m.cpp)
target_link_libraries(deps remoteexecution)

install(TARGETS ${BINARY} RUNTIME DESTINATION bin)

if(${CMAKE_SYSTEM_NAME} MATCHES "AIX" AND ${CMAKE_CXX_COMPILER_ID} MATCHES "GNU")
    message("Skipping all warnings due to GNU compiler + AIX system")
else()
    target_compile_options(remoteexecution PRIVATE -Wall -Werror=shadow ${DEBUG_FLAGS})
    target_compile_options(${BINARY} PRIVATE -Wall -Werror=shadow ${DEBUG_FLAGS})
    target_compile_options(casupload PRIVATE -Wall -Werror=shadow ${DEBUG_FLAGS})
    target_compile_options(deps PRIVATE -Wall -Werror=shadow ${DEBUG_FLAGS})
endif()
