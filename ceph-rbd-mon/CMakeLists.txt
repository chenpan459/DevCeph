cmake_minimum_required(VERSION 3.16)
project(ceph_rbd_only LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# third_party目录路径
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# Boost配置
set(BOOST_VERSION 1.82.0)
set(BOOST_ARCHIVE ${THIRD_PARTY_DIR}/boost_${BOOST_VERSION}.tar.bz2)
set(BOOST_SOURCE_DIR ${CMAKE_BINARY_DIR}/boost_${BOOST_VERSION})
set(BOOST_INSTALL_DIR ${CMAKE_BINARY_DIR}/boost_install)

# 检查Boost源代码是否存在
if(NOT EXISTS ${BOOST_SOURCE_DIR})
    message(STATUS "Extracting Boost ${BOOST_VERSION} from ${BOOST_ARCHIVE}")
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})

    # 解压Boost
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xjf ${BOOST_ARCHIVE}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        RESULT_VARIABLE EXTRACT_RESULT
    )

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract Boost archive")
    endif()
endif()

# 构建Boost
if(NOT EXISTS ${BOOST_INSTALL_DIR})
    message(STATUS "Building Boost ${BOOST_VERSION}")

    # 配置Boost构建
    execute_process(
        COMMAND ${BOOST_SOURCE_DIR}/bootstrap.sh --prefix=${BOOST_INSTALL_DIR}
        WORKING_DIRECTORY ${BOOST_SOURCE_DIR}
        RESULT_VARIABLE BOOTSTRAP_RESULT
    )

    if(NOT BOOTSTRAP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to bootstrap Boost")
    endif()

    # 构建Boost（只构建需要的组件）
    execute_process(
        COMMAND ${BOOST_SOURCE_DIR}/b2 install
            --prefix=${BOOST_INSTALL_DIR}
            --with-system
            --with-thread
            --with-random
            --with-iostreams
            --with-program_options
            --with-chrono
            --with-date_time
            --with-regex
            --with-atomic
            --with-context
            --with-coroutine
            threading=multi
            variant=release
            link=static
            runtime-link=shared
        WORKING_DIRECTORY ${BOOST_SOURCE_DIR}
        RESULT_VARIABLE BUILD_RESULT
    )

    if(NOT BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to build Boost")
    endif()
endif()

# 设置Boost路径
set(Boost_ROOT ${BOOST_INSTALL_DIR})
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)

# 查找Boost
find_package(Boost ${BOOST_VERSION} REQUIRED COMPONENTS
    system thread random iostreams program_options chrono date_time regex atomic context coroutine)

# 输出Boost信息
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost root: ${Boost_ROOT}")

include_directories(${Boost_INCLUDE_DIRS})

# 添加Boost库目录到链接路径
link_directories(${BOOST_INSTALL_DIR}/lib)

# 输出路径
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Ceph 自身头文件路径
include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/src/include
  ${CMAKE_SOURCE_DIR}/src/common
  ${CMAKE_SOURCE_DIR}/src/librados
  ${CMAKE_SOURCE_DIR}/src/librbd
  ${CMAKE_SOURCE_DIR}/src/libradosstriper
  ${CMAKE_SOURCE_DIR}/src/messages
  ${CMAKE_SOURCE_DIR}/src/msg
  ${CMAKE_SOURCE_DIR}/src/osdc
  

)

# 查找外部依赖
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# 查找fmt库（尝试系统安装，如果找不到则构建）
find_package(fmt QUIET)
if(NOT fmt_FOUND)
    message(STATUS "fmt not found in system, will use internal implementation")
    # 如果需要，可以在这里添加从third_party构建fmt的逻辑
    # 或者使用项目中已有的实现
endif()

# 添加子模块
add_subdirectory(src)
