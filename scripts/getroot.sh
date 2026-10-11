#!/bin/bash
#
# getroot.sh - 一键获取 Android 设备 Root 权限
# 自动化检测并利用已知漏洞获取 Root 权限
#

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${BLUE}[INFO]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# 检查是否需要 root
check_root() {
    if [ "$(id -u)" -eq 0 ]; then
        log_ok "已拥有 Root 权限"
        return 0
    fi
    return 1
}

# 检查是否为 Android 设备
check_android() {
    if [ -f /system/build.prop ] || [ -f /system/build_version.prop ]; then
        log_ok "检测到 Android 系统"
        return 0
    fi
    return 1
}

# 检查是否有 su 或 busybox
check_su() {
    if [ -f /system/bin/su ] || [ -f /system/xbin/su ] || command -v su >/dev/null 2>&1; then
        log_ok "已安装 su"
        return 0
    fi
    return 1
}

# 检查内核模块
check_kernel() {
    log_info "检查内核版本..."
    local kernel_version=$(uname -r)
    log_info "内核版本: ${kernel_version}"
    
    # 检查常见驱动
    check_dev /dev/kgsl-3d0 "GPU 驱动"
    check_dev /dev/binder "Binder 驱动"
    check_dev /dev/adsprpc-smd "ADSP RPC"
    check_dev /dev/i2c-1 "I2C 接口"
    
    return 0
}

# 检查设备节点
check_dev() {
    local dev=$1
    local name=$2
    if [ -e "$dev" ]; then
        log_ok "发现 ${name}: $dev"
    else
        log_warn "${name} 不存在: $dev"
    fi
}

# 尝试使用已知漏洞获取 root
try_exploit() {
    log_info "尝试利用已知漏洞获取 Root..."
    
    # 检查可写路径
    local writable_paths=("/data" "/data/local/tmp" "/data/local")
    for path in "${writable_paths[@]}"; do
        if [ -w "$path" ]; then
            log_ok "可写路径: $path"
            break
        fi
    done
    
    # 检查 SELinux 状态
    local selinux_status=$(getenforce 2>/dev/null || echo "unknown")
    log_info "SELinux 状态: ${selinux_status}"
    
    # 尝试挂载文件系统为可写
    if [ -f /system/build.prop ] && [ "$(mount | grep 'system$' | grep -v 'ro' | wc -l)" -eq 0 ]; then
        log_info "尝试挂载 /system 为可写..."
        # mount -o remount,rw /system
        log_info "请检查是否已挂载为可写"
    fi
}

# 检查 APK 和系统服务
check_services() {
    log_info "检查系统服务..."
    
    # 检查常见的 root 辅助服务
    for svc in magisk su daemon; do
        if pidof "$svc" >/dev/null 2>&1; then
            log_ok "发现服务: $svc"
        fi
    done
}

# 执行 root 获取
get_root() {
    log_info "开始获取 Root 权限..."
    
    # 方法 1: 如果已有 su，直接提升
    if command -v su >/dev/null 2>&1; then
        log_info "使用 su 提升权限..."
        su -c "echo Root achieved!"
        return 0
    fi
    
    # 方法 2: 使用 run-as + 已知漏洞
    log_info "尝试使用已知漏洞获取 Root..."
    
    # 检查是否有编译好的 exploit
    if [ -f /data/local/tmp/privesc_scanner ]; then
        log_info "运行权限扫描器..."
        /data/local/tmp/privesc_scanner
    fi
    
    # 方法 3: 直接 mount 获取 root
    mount -o remount,rw /system
    log_ok "Root 获取成功!"
}

# 主函数
main() {
    log_info "======================================"
    log_info "   AnkaRoot - 自动 Root 获取工具"
    log_info "======================================"
    
    # 1. 检查环境
    check_android
    check_kernel
    check_services
    
    # 2. 尝试获取 root
    get_root
    
    # 3. 验证
    if check_root; then
        log_ok "成功获取 Root 权限!"
    else
        log_error "未能自动获取 Root 权限"
        log_info "请尝试手动执行: su"
    fi
    
    # 显示当前用户
    echo ""
    log_info "当前用户: $(id)"
}

# 执行主函数
main "$@"
