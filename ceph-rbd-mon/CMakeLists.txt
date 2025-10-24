cmake_minimum_required(VERSION 3.16)
project(ceph_rbd_only LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# third_party目录路径
set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party)

# Boost配置
set(BOOST_VERSION 1.82.0)
set(BOOST_VERSION_UNDERSCORE 1_82_0)
set(BOOST_SOURCE_DIR ${CMAKE_BINARY_DIR}/boost_${BOOST_VERSION_UNDERSCORE})
set(BOOST_INSTALL_DIR ${CMAKE_BINARY_DIR}/boost_install)

# 查找Boost归档文件（支持多种命名格式）
file(GLOB BOOST_ARCHIVE_CANDIDATES
    "${THIRD_PARTY_DIR}/boost_${BOOST_VERSION}.tar.bz2"
    "${THIRD_PARTY_DIR}/boost-${BOOST_VERSION}.tar.bz2"
    "${THIRD_PARTY_DIR}/boost_${BOOST_VERSION}.tar.gz"
    "${THIRD_PARTY_DIR}/boost-${BOOST_VERSION}.tar.gz"
    "${THIRD_PARTY_DIR}/boost_${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    "${THIRD_PARTY_DIR}/boost-${BOOST_VERSION_UNDERSCORE}.tar.bz2"
    "${THIRD_PARTY_DIR}/boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
    "${THIRD_PARTY_DIR}/boost-${BOOST_VERSION_UNDERSCORE}.tar.gz"
)

# 选择第一个找到的文件
set(BOOST_ARCHIVE "")
foreach(CANDIDATE ${BOOST_ARCHIVE_CANDIDATES})
    if(EXISTS ${CANDIDATE})
        set(BOOST_ARCHIVE ${CANDIDATE})
        break()
    endif()
endforeach()

# 检查Boost源代码是否存在
if(NOT EXISTS ${BOOST_SOURCE_DIR})
    message(STATUS "Boost source directory not found: ${BOOST_SOURCE_DIR}")
    message(STATUS "Looking for Boost archive in third_party directory...")
    message(STATUS "Found Boost archive: ${BOOST_ARCHIVE}")
    message(STATUS "Extracting Boost ${BOOST_VERSION} from ${BOOST_ARCHIVE}")

    # 检查Boost归档文件是否存在
    if(NOT BOOST_ARCHIVE)
        message(FATAL_ERROR "Boost archive not found in third_party directory")
        message(FATAL_ERROR "Looking for Boost version: ${BOOST_VERSION}")
        message(FATAL_ERROR "Supported formats:")
        message(FATAL_ERROR "  - boost_${BOOST_VERSION}.tar.bz2")
        message(FATAL_ERROR "  - boost-${BOOST_VERSION}.tar.bz2")
        message(FATAL_ERROR "  - boost_${BOOST_VERSION}.tar.gz")
        message(FATAL_ERROR "  - boost-${BOOST_VERSION}.tar.gz")
        message(FATAL_ERROR "")
        message(FATAL_ERROR "Current third_party directory contents:")
        file(GLOB THIRD_PARTY_FILES "${THIRD_PARTY_DIR}/*")
        foreach(FILE ${THIRD_PARTY_FILES})
            message(STATUS "  ${FILE}")
        endforeach()
        message(FATAL_ERROR "")
        message(FATAL_ERROR "Please ensure one of the supported Boost archive formats exists in third_party/")
    endif()

    # 创建构建目录
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR})

    # 根据文件扩展名选择正确的解压命令
    if(BOOST_ARCHIVE MATCHES "\\.tar\\.bz2$")
        set(TAR_OPTIONS -xjf)
        set(CMAKE_TAR_OPTIONS xjf)
    elseif(BOOST_ARCHIVE MATCHES "\\.tar\\.gz$")
        set(TAR_OPTIONS -xzf)
        set(CMAKE_TAR_OPTIONS xzf)
    else()
        message(FATAL_ERROR "Unsupported Boost archive format: ${BOOST_ARCHIVE}")
    endif()

    # 解压Boost（使用更兼容的命令）
    if(UNIX)
        # Unix/Linux系统使用系统tar命令
        execute_process(
            COMMAND tar ${TAR_OPTIONS} ${BOOST_ARCHIVE}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE EXTRACT_RESULT
            OUTPUT_VARIABLE EXTRACT_OUTPUT
            ERROR_VARIABLE EXTRACT_ERROR
        )
    else()
        # Windows或其他系统使用CMake的tar命令
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar ${CMAKE_TAR_OPTIONS} ${BOOST_ARCHIVE}
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            RESULT_VARIABLE EXTRACT_RESULT
            OUTPUT_VARIABLE EXTRACT_OUTPUT
            ERROR_VARIABLE EXTRACT_ERROR
        )
    endif()

    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract Boost archive: ${BOOST_ARCHIVE}")
        message(FATAL_ERROR "Error: ${EXTRACT_ERROR}")
        message(FATAL_ERROR "Output: ${EXTRACT_OUTPUT}")
    endif()

    # 验证解压结果
    if(NOT EXISTS ${BOOST_SOURCE_DIR})
        message(FATAL_ERROR "Boost source directory not found after extraction: ${BOOST_SOURCE_DIR}")
    endif()

    message(STATUS "Successfully extracted Boost to ${BOOST_SOURCE_DIR}")
endif()

# 构建Boost
if(NOT EXISTS ${BOOST_INSTALL_DIR})
    message(STATUS "Building Boost ${BOOST_VERSION}")

    # 检查是否在Unix系统
    if(UNIX)
        # 配置Boost构建（Unix/Linux）
        execute_process(
            COMMAND bash -c "cd ${BOOST_SOURCE_DIR} && ./bootstrap.sh --prefix=${BOOST_INSTALL_DIR}"
            RESULT_VARIABLE BOOTSTRAP_RESULT
            OUTPUT_VARIABLE BOOTSTRAP_OUTPUT
            ERROR_VARIABLE BOOTSTRAP_ERROR
        )

        if(NOT BOOTSTRAP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to bootstrap Boost. Error: ${BOOTSTRAP_ERROR}")
        endif()

        # 构建Boost（只构建需要的组件）
        execute_process(
            COMMAND bash -c "cd ${BOOST_SOURCE_DIR} && ./b2 install --prefix=${BOOST_INSTALL_DIR} --with-system --with-thread --with-random --with-iostreams --with-program_options --with-chrono --with-date_time --with-regex --with-atomic --with-context --with-coroutine threading=multi variant=release link=static runtime-link=shared"
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )

        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build Boost. Error: ${BUILD_ERROR}")
        endif()
    else()
        # Windows或其他系统的构建逻辑
        execute_process(
            COMMAND ${BOOST_SOURCE_DIR}/bootstrap.bat
            RESULT_VARIABLE BOOTSTRAP_RESULT
            OUTPUT_VARIABLE BOOTSTRAP_OUTPUT
            ERROR_VARIABLE BOOTSTRAP_ERROR
        )

        if(NOT BOOTSTRAP_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to bootstrap Boost. Error: ${BOOTSTRAP_ERROR}")
        endif()

        # 构建Boost
        execute_process(
            COMMAND ${BOOST_SOURCE_DIR}/b2.exe install --prefix=${BOOST_INSTALL_DIR} --with-system --with-thread --with-random --with-iostreams --with-program_options --with-chrono --with-date_time --with-regex --with-atomic --with-context --with-coroutine threading=multi variant=release link=static
            RESULT_VARIABLE BUILD_RESULT
            OUTPUT_VARIABLE BUILD_OUTPUT
            ERROR_VARIABLE BUILD_ERROR
        )

        if(NOT BUILD_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to build Boost. Error: ${BUILD_ERROR}")
        endif()
    endif()

    # 验证构建结果
    if(NOT EXISTS ${BOOST_INSTALL_DIR})
        message(FATAL_ERROR "Boost install directory not found after build: ${BOOST_INSTALL_DIR}")
    endif()

    message(STATUS "Successfully built Boost to ${BOOST_INSTALL_DIR}")
endif()

# 设置Boost路径
set(Boost_ROOT ${BOOST_INSTALL_DIR})
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_DEBUG_RUNTIME OFF)

# 查找Boost
find_package(Boost ${BOOST_VERSION} QUIET COMPONENTS
    system thread random iostreams program_options chrono date_time regex atomic context coroutine)

if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found after build. Boost_ROOT=${Boost_ROOT}")
endif()

# 输出Boost信息
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost root: ${Boost_ROOT}")

include_directories(${Boost_INCLUDE_DIRS})

# 添加Boost库目录到链接路径
link_directories(${BOOST_INSTALL_DIR}/lib)

# 设置Boost库变量供其他组件使用
set(BOOST_LIBRARIES ${Boost_LIBRARIES})
set(BOOST_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})

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
