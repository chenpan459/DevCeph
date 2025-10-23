# 只包含存在的源文件
set(lib_mon_srcs
  Paxos.cc
  PaxosService.cc
  OSDMonitor.cc
  MDSMonitor.cc
  CommandHandler.cc
  FSCommands.cc
  MgrMonitor.cc
  MgrStatMonitor.cc
  Monitor.cc
  MonmapMonitor.cc
  LogMonitor.cc
  AuthMonitor.cc
  ConfigMap.cc
  ConfigMonitor.cc
  Elector.cc
  ElectionLogic.cc
  ConnectionTracker.cc
  HealthMonitor.cc
  KVMonitor.cc)

# mgr_cap_obj目标不存在，已移除引用

# GSSAPI相关文件不存在，已移除引用
# if(HAVE_GSSAPI)
#   list(APPEND lib_mon_srcs
#     ${CMAKE_SOURCE_DIR}/src/auth/krb/KrbServiceHandler.cpp)
# endif()

add_library(mon STATIC
  ${lib_mon_srcs})

# 包含路径已在主CMakeLists.txt中全局设置
# 只有在fmt库存在时才链接
if(fmt_FOUND AND TARGET fmt::fmt)
  target_link_libraries(mon fmt::fmt)
endif()
if(WITH_JAEGER)
  target_link_libraries(mon jaeger_base)
endif()
