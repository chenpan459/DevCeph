# 最小CMake版本
cmake_minimum_required(VERSION 3.16)

# 项目定义
project(ceph-minimal
  VERSION 19.2.2
  LANGUAGES CXX C)

# 设置输出目录
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 包含目录
include_directories(
  ${PROJECT_BINARY_DIR}/src/include
  ${PROJECT_SOURCE_DIR}/src)

# 启用C++17标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找必需的包
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# 查找src目录下所有包含CMakeLists.txt的子目录并添加
file(GLOB_RECURSE SUB_CMAKELISTS 
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  "src/*/CMakeLists.txt"
)

# 存储已添加的子目录路径，避免重复添加
set(ADDED_SUBDIRS "")

foreach(cmake_file ${SUB_CMAKELISTS})
  # 提取目录路径
  get_filename_component(subdir ${cmake_file} DIRECTORY)
  
  # 检查是否已经添加过该子目录（避免重复添加）
  list(FIND ADDED_SUBDIRS ${subdir} subdir_index)
  if(${subdir_index} EQUAL -1)
    # 添加子目录到构建系统
    add_subdirectory(${subdir})
    # 记录已添加的子目录
    list(APPEND ADDED_SUBDIRS ${subdir})
  endif()
endforeach()

# 收集未被子CMakeLists.txt处理的源文件
# 先获取所有源文件
file(GLOB_RECURSE ALL_SOURCES "src/*.cc" "src/*.cpp" "src/*.c")

# 获取子目录中CMakeLists.txt所在的目录列表，用于排除已被处理的源文件
set(SUBDIR_LIST "")
foreach(cmake_file ${SUB_CMAKELISTS})
  get_filename_component(subdir_path ${cmake_file} DIRECTORY)
  list(APPEND SUBDIR_LIST ${subdir_path})
endforeach()

# 过滤出不在子目录中的源文件
set(ROOT_SOURCES "")
foreach(source_file ${ALL_SOURCES})
  set(in_subdir FALSE)
  foreach(subdir ${SUBDIR_LIST})
    string(FIND ${source_file} ${subdir} subdir_pos)
    if(NOT ${subdir_pos} EQUAL -1)
      set(in_subdir TRUE)
      break()
    endif()
  endforeach()
  
  if(NOT in_subdir)
    list(APPEND ROOT_SOURCES ${source_file})
  endif()
endforeach()

# 只有当存在未被子目录处理的源文件时才创建主可执行文件
if(ROOT_SOURCES)
  # 创建可执行文件
  add_executable(ceph ${ROOT_SOURCES})

  # 链接库
  target_link_libraries(ceph 
    PRIVATE Threads::Threads 
    OpenSSL::SSL 
    OpenSSL::Crypto 
    ZLIB::ZLIB)

  # 如果在Linux上，添加相应的定义
  if(CMAKE_SYSTEM_NAME MATCHES "Linux")
    target_compile_definitions(ceph PRIVATE __linux__)
  endif()
endif()