cmake_minimum_required(VERSION 3.16)

# 项目名称和版本
project(ceph-rbd-mon VERSION 1.0.0 LANGUAGES C CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 设置安装路径
set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Installation Directory")

# 设置库安装路径
set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Library Directory")
set(CMAKE_INSTALL_INCLUDEDIR "include" CACHE PATH "Include Directory")
set(CMAKE_INSTALL_BINDIR "bin" CACHE PATH "Binary Directory")

# 设置CEPH相关路径
set(CEPH_INSTALL_PKGLIBDIR "${CMAKE_INSTALL_LIBDIR}/ceph")
set(CEPH_INSTALL_FULL_PKGLIBDIR "${CMAKE_INSTALL_PREFIX}/${CEPH_INSTALL_PKGLIBDIR}")
set(CEPH_INSTALL_DATADIR "${CMAKE_INSTALL_PREFIX}/share/ceph")

# 设置共享库选项
option(ENABLE_SHARED "Build shared libraries" ON)
option(ENABLE_STATIC "Build static libraries" ON)

# 设置CEPH功能选项
option(WITH_RBD "Build RBD support" ON)
option(WITH_LIBRADOSSTRIPER "Build libradosstriper support" ON)
option(WITH_RADOSGW "Build RADOS Gateway support" OFF)
option(WITH_LIBCEPHFS "Build libcephfs support" OFF)
option(WITH_SYSTEMD "Build with systemd support" OFF)
option(WITH_CEPH_DEBUG_MUTEX "Build with debug mutex support" OFF)
option(WITH_EVENTTRACE "Build with event tracing support" OFF)
option(WITH_LTTNG "Build with LTTng tracing support" OFF)
option(WITH_JAEGER "Build with Jaeger tracing support" OFF)
option(WITH_DPDK "Build with DPDK support" OFF)
option(WITH_RBD_RWL "Build with RBD RWL cache support" OFF)
option(WITH_RBD_SSD_CACHE "Build with RBD SSD cache support" OFF)

# 设置平台相关选项
if(WIN32)
    option(MINGW "Use MinGW compiler" OFF)
    set(HAVE_LINK_EXCLUDE_LIBS FALSE)
    set(HAVE_LINK_VERSION_SCRIPT FALSE)
else()
    set(HAVE_LINK_EXCLUDE_LIBS TRUE)
    set(HAVE_LINK_VERSION_SCRIPT TRUE)
endif()

# 设置架构相关选项
option(HAVE_INTEL "Intel architecture optimizations" OFF)
option(HAVE_POWER8 "Power8 architecture optimizations" OFF)
option(HAVE_ARMV8_CRC "ARMv8 CRC optimizations" OFF)
option(HAVE_PPC64LE "PowerPC 64-bit little endian" OFF)
option(HAVE_NASM_X64 "NASM x64 assembler support" OFF)

# 设置功能检测选项
option(HAVE_KEYUTILS "Keyutils support" OFF)
option(HAVE_RDMA "RDMA support" OFF)
option(HAVE_GSSAPI "GSSAPI support" OFF)
option(HAVE_LIBCRYPTSETUP "libcryptsetup support" OFF)
option(HAVE_UDEV "udev support" OFF)
option(HAVE_VTA "Variable tracking assignments" OFF)

# 设置编译器相关选项
option(WITH_STATIC_LIBSTDCXX "Link statically with libstdc++" OFF)

# 查找依赖库
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter)

# 查找OpenSSL
find_package(OpenSSL REQUIRED)

# 查找fmt库
find_package(fmt REQUIRED)

# 查找其他可选依赖
if(WITH_SYSTEMD)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SYSTEMD REQUIRED libsystemd)
endif()

if(HAVE_LIBCRYPTSETUP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCRYPTSETUP REQUIRED libcryptsetup)
endif()

if(HAVE_UDEV)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UDEV REQUIRED libudev)
endif()

if(HAVE_GSSAPI)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GSSAPI REQUIRED krb5-gssapi)
endif()

if(WITH_DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DPDK REQUIRED libdpdk)
endif()

if(WITH_RBD_RWL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PMDK REQUIRED libpmemobj)
endif()

# 设置包含目录
include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/src/common)
include_directories(${CMAKE_SOURCE_DIR}/src/msg)
include_directories(${CMAKE_SOURCE_DIR}/src/mon)
include_directories(${CMAKE_SOURCE_DIR}/src/librados)
include_directories(${CMAKE_SOURCE_DIR}/src/libradosstriper)
include_directories(${CMAKE_SOURCE_DIR}/src/librbd)
include_directories(${CMAKE_SOURCE_DIR}/src/messages)

# 设置链接库
set(EXTRALIBS "")
set(CRYPTO_LIBS ${OPENSSL_LIBRARIES})

# 设置共享库类型
if(ENABLE_SHARED)
    set(CEPH_SHARED SHARED)
else()
    set(CEPH_SHARED STATIC)
endif()

# 创建版本头文件
configure_file(
    ${CMAKE_SOURCE_DIR}/src/include/config-h.in.cmake
    ${CMAKE_BINARY_DIR}/src/include/ceph_ver.h
    @ONLY
)

# 添加子目录
add_subdirectory(src/include)
add_subdirectory(src/common)
add_subdirectory(src/msg)
add_subdirectory(src/mon)
add_subdirectory(src/librados)
add_subdirectory(src/libradosstriper)

if(WITH_RBD)
    add_subdirectory(src/librbd)
endif()

# 创建主库目标
add_library(ceph-rbd-mon ${CEPH_SHARED}
    $<TARGET_OBJECTS:common-common-objs>
    $<TARGET_OBJECTS:common-msg-objs>
    $<TARGET_OBJECTS:common_buffer_obj>
    $<TARGET_OBJECTS:common_texttable_obj>
    $<TARGET_OBJECTS:common_prioritycache_obj>
    $<TARGET_OBJECTS:crc32>
)

# 包含路径已在上面全局设置

# 链接库
target_link_libraries(ceph-rbd-mon PRIVATE
    librados
    radosstriper
    mon
    ${OPENSSL_LIBRARIES}
    ${CRYPTO_LIBS}
    ${EXTRALIBS}
    Threads::Threads
)

if(WITH_RBD)
    target_link_libraries(ceph-rbd-mon PRIVATE librbd)
endif()

if(WITH_SYSTEMD)
    target_link_libraries(ceph-rbd-mon PRIVATE ${SYSTEMD_LIBRARIES})
endif()

if(HAVE_LIBCRYPTSETUP)
    target_link_libraries(ceph-rbd-mon PRIVATE ${LIBCRYPTSETUP_LIBRARIES})
endif()

if(HAVE_UDEV)
    target_link_libraries(ceph-rbd-mon PRIVATE ${UDEV_LIBRARIES})
endif()

if(HAVE_GSSAPI)
    target_link_libraries(ceph-rbd-mon PRIVATE ${GSSAPI_LIBRARIES})
endif()

if(WITH_DPDK)
    target_link_libraries(ceph-rbd-mon PRIVATE ${DPDK_LIBRARIES})
endif()

if(WITH_RBD_RWL)
    target_link_libraries(ceph-rbd-mon PRIVATE ${PMDK_LIBRARIES})
endif()

# 设置库属性
if(ENABLE_SHARED)
    set_target_properties(ceph-rbd-mon PROPERTIES
        OUTPUT_NAME ceph-rbd-mon
        VERSION 1.0.0
        SOVERSION 1
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    
    if(HAVE_LINK_EXCLUDE_LIBS AND NOT WIN32)
        set_property(TARGET ceph-rbd-mon APPEND_STRING PROPERTY
            LINK_FLAGS " -Wl,--exclude-libs,ALL")
    endif()
    
    if(HAVE_LINK_VERSION_SCRIPT AND NOT WIN32)
        # 可以添加版本脚本
    endif()
    
    if(WITH_STATIC_LIBSTDCXX)
        set_property(TARGET ceph-rbd-mon APPEND_STRING PROPERTY
            LINK_FLAGS " -static-libstdc++ -static-libgcc")
    endif()
endif()

# 安装目标
install(TARGETS ceph-rbd-mon DESTINATION ${CMAKE_INSTALL_LIBDIR})

# 安装头文件
install(DIRECTORY src/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# 创建示例程序
add_executable(ceph-rbd-mon-example
    examples/main.cc
)

# 包含路径已在上面全局设置

target_link_libraries(ceph-rbd-mon-example PRIVATE
    ceph-rbd-mon
    ${OPENSSL_LIBRARIES}
    Threads::Threads
)

install(TARGETS ceph-rbd-mon-example DESTINATION ${CMAKE_INSTALL_BINDIR})

# 创建pkg-config文件
configure_file(
    ${CMAKE_SOURCE_DIR}/ceph-rbd-mon.pc.in
    ${CMAKE_BINARY_DIR}/ceph-rbd-mon.pc
    @ONLY
)

install(FILES ${CMAKE_BINARY_DIR}/ceph-rbd-mon.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# 显示配置信息
message(STATUS "=== Ceph RBD Monitor Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Shared libraries: ${ENABLE_SHARED}")
message(STATUS "Static libraries: ${ENABLE_STATIC}")
message(STATUS "RBD support: ${WITH_RBD}")
message(STATUS "libradosstriper support: ${WITH_LIBRADOSSTRIPER}")
message(STATUS "Systemd support: ${WITH_SYSTEMD}")
message(STATUS "=====================================")
