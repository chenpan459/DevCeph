#!/bin/bash

# Ceph RBD Monitor 构建脚本

set -e

# 默认配置
BUILD_TYPE="Release"
INSTALL_PREFIX="/usr/local"
ENABLE_SHARED="ON"
ENABLE_STATIC="ON"
WITH_RBD="ON"
WITH_LIBRADOSSTRIPER="ON"
WITH_SYSTEMD="OFF"
WITH_CEPH_DEBUG_MUTEX="OFF"
WITH_EVENTTRACE="OFF"
WITH_LTTNG="OFF"
WITH_JAEGER="OFF"
WITH_DPDK="OFF"
WITH_RBD_RWL="OFF"
WITH_RBD_SSD_CACHE="OFF"
HAVE_INTEL="OFF"
HAVE_POWER8="OFF"
HAVE_ARMV8_CRC="OFF"
HAVE_PPC64LE="OFF"
HAVE_NASM_X64="OFF"
HAVE_KEYUTILS="OFF"
HAVE_RDMA="OFF"
HAVE_GSSAPI="OFF"
HAVE_LIBCRYPTSETUP="OFF"
HAVE_UDEV="OFF"
HAVE_VTA="OFF"
WITH_STATIC_LIBSTDCXX="OFF"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印帮助信息
print_help() {
    echo "Ceph RBD Monitor 构建脚本"
    echo ""
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help              显示此帮助信息"
    echo "  -t, --type TYPE         构建类型 (Debug/Release) [默认: Release]"
    echo "  -p, --prefix PREFIX     安装前缀 [默认: /usr/local]"
    echo "  --shared               启用共享库构建 [默认: ON]"
    echo "  --no-shared            禁用共享库构建"
    echo "  --static               启用静态库构建 [默认: ON]"
    echo "  --no-static            禁用静态库构建"
    echo "  --rbd                  启用RBD支持 [默认: ON]"
    echo "  --no-rbd               禁用RBD支持"
    echo "  --radosstriper         启用libradosstriper支持 [默认: ON]"
    echo "  --no-radosstriper      禁用libradosstriper支持"
    echo "  --systemd              启用systemd支持"
    echo "  --debug-mutex          启用调试互斥锁"
    echo "  --eventtrace           启用事件跟踪"
    echo "  --lttng                启用LTTng跟踪"
    echo "  --jaeger               启用Jaeger跟踪"
    echo "  --dpdk                 启用DPDK支持"
    echo "  --rbd-rwl              启用RBD RWL缓存"
    echo "  --rbd-ssd-cache        启用RBD SSD缓存"
    echo "  --intel                启用Intel架构优化"
    echo "  --power8               启用Power8架构优化"
    echo "  --armv8-crc            启用ARMv8 CRC优化"
    echo "  --clean                清理构建目录"
    echo "  --install              编译后安装"
    echo "  --example              编译示例程序"
    echo ""
    echo "示例:"
    echo "  $0                      # 默认构建"
    echo "  $0 -t Debug             # 调试构建"
    echo "  $0 --clean              # 清理构建目录"
    echo "  $0 --install            # 构建并安装"
    echo "  $0 --example            # 构建示例程序"
}

# 打印信息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
check_dependencies() {
    print_info "检查依赖..."
    
    # 检查CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake 未安装，请先安装CMake"
        exit 1
    fi
    
    # 检查编译器
    if ! command -v g++ &> /dev/null && ! command -v clang++ &> /dev/null; then
        print_error "未找到C++编译器，请安装g++或clang++"
        exit 1
    fi
    
    # 检查pkg-config
    if ! command -v pkg-config &> /dev/null; then
        print_error "pkg-config 未安装，请先安装pkg-config"
        exit 1
    fi
    
    # 检查OpenSSL
    if ! pkg-config --exists openssl; then
        print_error "OpenSSL 未安装，请先安装OpenSSL开发包"
        exit 1
    fi
    
    # 检查fmt
    if ! pkg-config --exists fmt; then
        print_error "fmt库 未安装，请先安装fmt开发包"
        exit 1
    fi
    
    print_success "依赖检查完成"
}

# 清理构建目录
clean_build() {
    print_info "清理构建目录..."
    if [ -d "build" ]; then
        rm -rf build
        print_success "构建目录已清理"
    else
        print_warning "构建目录不存在"
    fi
}

# 创建构建目录
create_build_dir() {
    if [ ! -d "build" ]; then
        print_info "创建构建目录..."
        mkdir build
    fi
}

# 配置CMake
configure_cmake() {
    print_info "配置CMake..."
    
    cd build
    
    # 构建CMake命令
    CMAKE_CMD="cmake .."
    CMAKE_CMD="$CMAKE_CMD -DCMAKE_BUILD_TYPE=$BUILD_TYPE"
    CMAKE_CMD="$CMAKE_CMD -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX"
    CMAKE_CMD="$CMAKE_CMD -DENABLE_SHARED=$ENABLE_SHARED"
    CMAKE_CMD="$CMAKE_CMD -DENABLE_STATIC=$ENABLE_STATIC"
    CMAKE_CMD="$CMAKE_CMD -DWITH_RBD=$WITH_RBD"
    CMAKE_CMD="$CMAKE_CMD -DWITH_LIBRADOSSTRIPER=$WITH_LIBRADOSSTRIPER"
    CMAKE_CMD="$CMAKE_CMD -DWITH_SYSTEMD=$WITH_SYSTEMD"
    CMAKE_CMD="$CMAKE_CMD -DWITH_CEPH_DEBUG_MUTEX=$WITH_CEPH_DEBUG_MUTEX"
    CMAKE_CMD="$CMAKE_CMD -DWITH_EVENTTRACE=$WITH_EVENTTRACE"
    CMAKE_CMD="$CMAKE_CMD -DWITH_LTTNG=$WITH_LTTNG"
    CMAKE_CMD="$CMAKE_CMD -DWITH_JAEGER=$WITH_JAEGER"
    CMAKE_CMD="$CMAKE_CMD -DWITH_DPDK=$WITH_DPDK"
    CMAKE_CMD="$CMAKE_CMD -DWITH_RBD_RWL=$WITH_RBD_RWL"
    CMAKE_CMD="$CMAKE_CMD -DWITH_RBD_SSD_CACHE=$WITH_RBD_SSD_CACHE"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_INTEL=$HAVE_INTEL"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_POWER8=$HAVE_POWER8"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_ARMV8_CRC=$HAVE_ARMV8_CRC"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_PPC64LE=$HAVE_PPC64LE"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_NASM_X64=$HAVE_NASM_X64"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_KEYUTILS=$HAVE_KEYUTILS"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_RDMA=$HAVE_RDMA"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_GSSAPI=$HAVE_GSSAPI"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_LIBCRYPTSETUP=$HAVE_LIBCRYPTSETUP"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_UDEV=$HAVE_UDEV"
    CMAKE_CMD="$CMAKE_CMD -DHAVE_VTA=$HAVE_VTA"
    CMAKE_CMD="$CMAKE_CMD -DWITH_STATIC_LIBSTDCXX=$WITH_STATIC_LIBSTDCXX"
    
    print_info "执行: $CMAKE_CMD"
    eval $CMAKE_CMD
    
    if [ $? -eq 0 ]; then
        print_success "CMake配置完成"
    else
        print_error "CMake配置失败"
        exit 1
    fi
    
    cd ..
}

# 编译
build_project() {
    print_info "开始编译..."
    
    cd build
    
    # 获取CPU核心数
    if command -v nproc &> /dev/null; then
        CORES=$(nproc)
    else
        CORES=4
    fi
    
    make -j$CORES
    
    if [ $? -eq 0 ]; then
        print_success "编译完成"
    else
        print_error "编译失败"
        exit 1
    fi
    
    cd ..
}

# 安装
install_project() {
    print_info "开始安装..."
    
    cd build
    sudo make install
    
    if [ $? -eq 0 ]; then
        print_success "安装完成"
    else
        print_error "安装失败"
        exit 1
    fi
    
    cd ..
}

# 编译示例程序
build_example() {
    print_info "编译示例程序..."
    
    cd build
    make ceph-rbd-mon-example
    
    if [ $? -eq 0 ]; then
        print_success "示例程序编译完成"
        print_info "示例程序位置: build/ceph-rbd-mon-example"
    else
        print_error "示例程序编译失败"
        exit 1
    fi
    
    cd ..
}

# 解析命令行参数
CLEAN=false
INSTALL=false
EXAMPLE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            print_help
            exit 0
            ;;
        -t|--type)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -p|--prefix)
            INSTALL_PREFIX="$2"
            shift 2
            ;;
        --shared)
            ENABLE_SHARED="ON"
            shift
            ;;
        --no-shared)
            ENABLE_SHARED="OFF"
            shift
            ;;
        --static)
            ENABLE_STATIC="ON"
            shift
            ;;
        --no-static)
            ENABLE_STATIC="OFF"
            shift
            ;;
        --rbd)
            WITH_RBD="ON"
            shift
            ;;
        --no-rbd)
            WITH_RBD="OFF"
            shift
            ;;
        --radosstriper)
            WITH_LIBRADOSSTRIPER="ON"
            shift
            ;;
        --no-radosstriper)
            WITH_LIBRADOSSTRIPER="OFF"
            shift
            ;;
        --systemd)
            WITH_SYSTEMD="ON"
            shift
            ;;
        --debug-mutex)
            WITH_CEPH_DEBUG_MUTEX="ON"
            shift
            ;;
        --eventtrace)
            WITH_EVENTTRACE="ON"
            shift
            ;;
        --lttng)
            WITH_LTTNG="ON"
            shift
            ;;
        --jaeger)
            WITH_JAEGER="ON"
            shift
            ;;
        --dpdk)
            WITH_DPDK="ON"
            shift
            ;;
        --rbd-rwl)
            WITH_RBD_RWL="ON"
            shift
            ;;
        --rbd-ssd-cache)
            WITH_RBD_SSD_CACHE="ON"
            shift
            ;;
        --intel)
            HAVE_INTEL="ON"
            shift
            ;;
        --power8)
            HAVE_POWER8="ON"
            shift
            ;;
        --armv8-crc)
            HAVE_ARMV8_CRC="ON"
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        --install)
            INSTALL=true
            shift
            ;;
        --example)
            EXAMPLE=true
            shift
            ;;
        *)
            print_error "未知选项: $1"
            print_help
            exit 1
            ;;
    esac
done

# 主流程
main() {
    print_info "Ceph RBD Monitor 构建脚本启动"
    
    # 检查依赖
    check_dependencies
    
    # 清理构建目录
    if [ "$CLEAN" = true ]; then
        clean_build
        if [ "$INSTALL" = false ] && [ "$EXAMPLE" = false ]; then
            exit 0
        fi
    fi
    
    # 创建构建目录
    create_build_dir
    
    # 配置CMake
    configure_cmake
    
    # 编译
    build_project
    
    # 编译示例程序
    if [ "$EXAMPLE" = true ]; then
        build_example
    fi
    
    # 安装
    if [ "$INSTALL" = true ]; then
        install_project
    fi
    
    print_success "构建完成！"
    
    if [ "$EXAMPLE" = true ]; then
        print_info "运行示例程序:"
        print_info "  cd build && ./ceph-rbd-mon-example -h"
    fi
}

# 运行主流程
main
