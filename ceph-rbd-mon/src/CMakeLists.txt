# 简化配置 - 只保留RBD和MON需要的部分
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 基本编译选项
add_definitions(
  -DHAVE_CONFIG_H
  -D__CEPH__
  -D_REENTRANT
  -D_THREAD_SAFE
  -D__STDC_FORMAT_MACROS
  -D_FILE_OFFSET_BITS=64)

if(LINUX)
  add_definitions("-D_GNU_SOURCE")
endif()

add_compile_options(
  -Wall
  -Wextra
  -Wno-unused-function
  -Wno-unused-parameter)

# 添加Boost头文件路径
include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

# 添加third_party Boost库路径到链接路径
link_directories(${BOOST_INSTALL_DIR}/lib)

# 简化组件构建 - 只保留RBD和MON需要的部分
add_subdirectory(json_spirit)
add_subdirectory(common)
add_subdirectory(msg)
add_subdirectory(osdc)
add_subdirectory(mon)

# RBD相关组件
if(WITH_RBD)
  add_subdirectory(librbd)
endif()

# 安装头文件
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h")


