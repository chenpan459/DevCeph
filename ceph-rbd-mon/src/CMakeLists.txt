# 构建各个组件库
add_subdirectory(common)
add_subdirectory(librados)
add_subdirectory(libradosstriper)
add_subdirectory(librbd)
add_subdirectory(msg)
add_subdirectory(osdc)

# 创建主库（如果需要）
# add_library(ceph-rbd-mon INTERFACE)
# target_link_libraries(ceph-rbd-mon INTERFACE
#     ceph-common
#     librados
#     librbd
#     ${Boost_LIBRARIES}
#     ${OPENSSL_LIBRARIES}
#     Threads::Threads)
