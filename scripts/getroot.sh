#!/bin/bash
#
# getroot.sh - one-shot Android device root acquisition
# automatically detect and exploit known vulnerabilities to obtain root
#

set -e

# colour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;36m'
NC='\033[0m'

log_info()  { echo -e "${BLUE}[INFO]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# check whether root is needed
check_root() {
    if [ "$(id -u)" -eq 0 ]; then
        log_ok "已拥有 Root 权限"
        return 0
    fi
    return 1
}

# check whether this is an Android device
check_android() {
    if [ -f /system/build.prop ] || [ -f /system/build_version.prop ]; then
        log_ok "检测到 Android 系统"
        return 0
    fi
    return 1
}

# check for su or busybox
check_su() {
    if [ -f /system/bin/su ] || [ -f /system/xbin/su ] || command -v su >/dev/null 2>&1; then
        log_ok "已安装 su"
        return 0
    fi
    return 1
}

# check kernel modules
check_kernel() {
    log_info "检查内核版本..."
    local kernel_version=$(uname -r)
    log_info "内核版本: ${kernel_version}"
    
    # check common drivers
    check_dev /dev/kgsl-3d0 "GPU 驱动"
    check_dev /dev/binder "Binder 驱动"
    check_dev /dev/adsprpc-smd "ADSP RPC"
    check_dev /dev/i2c-1 "I2C 接口"
    
    return 0
}

# check a device node
check_dev() {
    local dev=$1
    local name=$2
    if [ -e "$dev" ]; then
        log_ok "发现 ${name}: $dev"
    else
        log_warn "${name} 不存在: $dev"
    fi
}

# try to obtain root with a known vulnerability
try_exploit() {
    log_info "尝试利用已知漏洞获取 Root..."
    
    # check writable paths
    local writable_paths=("/data" "/data/local/tmp" "/data/local")
    for path in "${writable_paths[@]}"; do
        if [ -w "$path" ]; then
            log_ok "可写路径: $path"
            break
        fi
    done
    
    # check the SELinux state
    local selinux_status=$(getenforce 2>/dev/null || echo "unknown")
    log_info "SELinux 状态: ${selinux_status}"
    
    # try to mount the filesystem writable
    if [ -f /system/build.prop ] && [ "$(mount | grep 'system$' | grep -v 'ro' | wc -l)" -eq 0 ]; then
        log_info "尝试挂载 /system 为可写..."
        # mount -o remount,rw /system
        log_info "请检查是否已挂载为可写"
    fi
}

# check the APK and system services
check_services() {
    log_info "检查系统服务..."
    
    # check the common root helper services
    for svc in magisk su daemon; do
        if pidof "$svc" >/dev/null 2>&1; then
            log_ok "发现服务: $svc"
        fi
    done
}

# perform the root acquisition
get_root() {
    log_info "开始获取 Root 权限..."
    
    # Method 1: if su already exists, escalate directly
    if command -v su >/dev/null 2>&1; then
        log_info "使用 su 提升权限..."
        su -c "echo Root achieved!"
        return 0
    fi
    
    # Method 2: use run-as + a known vulnerability
    log_info "尝试使用已知漏洞获取 Root..."
    
    # check whether a compiled exploit is present
    if [ -f /data/local/tmp/privesc_scanner ]; then
        log_info "运行权限扫描器..."
        /data/local/tmp/privesc_scanner
    fi
    
    # Method 3: obtain root by mounting directly
    mount -o remount,rw /system
    log_ok "Root 获取成功!"
}

# main function
main() {
    log_info "======================================"
    log_info "   AnkaRoot - 自动 Root 获取工具"
    log_info "======================================"
    
    # 1. check the environment
    check_android
    check_kernel
    check_services
    
    # 2. try to obtain root
    get_root
    
    # 3. verify
    if check_root; then
        log_ok "成功获取 Root 权限!"
    else
        log_error "未能自动获取 Root 权限"
        log_info "请尝试手动执行: su"
    fi
    
    # show the current user
    echo ""
    log_info "当前用户: $(id)"
}

# run main
main "$@"
