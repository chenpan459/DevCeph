set(libradosstriper_srcs
  libradosstriper.cc
  RadosStriperImpl.cc
  MultiAioCompletionImpl.cc)
add_library(radosstriper ${CEPH_SHARED}
  ${libradosstriper_srcs})

# 包含路径已在主CMakeLists.txt中全局设置
target_link_libraries(radosstriper
  PRIVATE
    librados
    librados_impl
    pthread ${CRYPTO_LIBS} ${EXTRALIBS})
set_target_properties(radosstriper PROPERTIES
  OUPUT_NAME radosstriper
  VERSION 1.0.0
  SOVERSION 1)

install(TARGETS radosstriper DESTINATION ${CMAKE_INSTALL_LIBDIR})
