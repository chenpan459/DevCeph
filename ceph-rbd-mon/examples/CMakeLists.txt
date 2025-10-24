# 示例程序构建配置

# 查找示例源文件
file(GLOB example_srcs *.cc *.cpp *.cxx)

if(example_srcs)
    foreach(example_src ${example_srcs})
        get_filename_component(example_name ${example_src} NAME_WE)

        # 创建可执行文件
        add_executable(${example_name} ${example_src})

        # 链接库
        target_link_libraries(${example_name} PRIVATE
            ceph-rbd-mon
            ${OPENSSL_LIBRARIES}
            ${FMT_LIBRARIES}
            Threads::Threads)

        # 包含目录
        target_include_directories(${example_name} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/include
            ${OPENSSL_INCLUDE_DIRS}
            ${FMT_INCLUDE_DIRS})

        # 编译选项
        target_compile_options(${example_name} PRIVATE
            ${OPENSSL_CFLAGS}
            ${FMT_CFLAGS})

        # 安装示例程序
        install(TARGETS ${example_name}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

        # 设置输出名称
        set_target_properties(${example_name} PROPERTIES
            OUTPUT_NAME ceph-rbd-mon-${example_name})

        message(STATUS "Configured example: ${example_name}")
    endforeach()
endif()

# 添加自定义目标来构建所有示例
if(example_srcs)
    add_custom_target(examples DEPENDS ${example_srcs})
    add_custom_target(ceph-rbd-mon-examples DEPENDS examples)
endif()
