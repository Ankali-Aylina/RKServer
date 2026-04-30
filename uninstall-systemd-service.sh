#!/bin/bash
#
# rkserver - systemd 服务卸载脚本
#
# 用法:
#   sudo ./uninstall-systemd-service.sh                  # 卸载系统服务
#   sudo ./uninstall-systemd-service.sh --force          # 强制卸载（不提示确认）
#   ./uninstall-systemd-service.sh --user                # 卸载用户服务
#
# 选项:
#   --user        卸载用户 systemd 服务（~/.config/systemd/user/）
#   --force       跳过确认提示
#   --help        显示此帮助信息

set -euo pipefail

SERVICE_NAME="rkserver"
INSTALL_MODE="system"
FORCE=false

# ============================================================
# 颜色定义
# ============================================================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# ============================================================
# 辅助函数
# ============================================================
print_banner() {
    local mode_label
    if [[ "$INSTALL_MODE" == "user" ]]; then
        mode_label="用户模式"
    else
        mode_label="系统服务"
    fi

    echo -e "${CYAN}================================================"
    echo -e "  ${BOLD}  ____  _  __  ____                           ${NC}${CYAN}"
    echo -e "  ${BOLD} |  _ \| |/ / / ___|  ___ _ ____   _____ _ __ ${NC}${CYAN}"
    echo -e "  ${BOLD} | |_) | ' /  \___ \ / _ \ '__\ \ / / _ \ '__|${NC}${CYAN}"
    echo -e "  ${BOLD} |  _ <| . \   ___) |  __/ |   \ V /  __/ |   ${NC}${CYAN}"
    echo -e "  ${BOLD} |_| \_\_|\_\ |____/ \___|_|    \_/ \___|_|   ${NC}${CYAN}"
    echo -e "${NC}"
    echo -e "${CYAN}================================================${NC}"
    echo -e "  ${BOLD}RKServer${NC} - RK Server"
    echo -e "  systemd 服务卸载脚本 | ${BOLD}${mode_label}${NC}"
    echo -e "${CYAN}================================================${NC}"
    echo ""
}

print_step() {
    local num=$1
    local desc=$2
    echo -e "  ${BOLD}[${num}]${NC} ${desc}"
}

print_ok() {
    echo -e "  ${GREEN}✓${NC} $1"
}

print_warn() {
    echo -e "  ${YELLOW}⚠${NC} $1"
}

print_err() {
    echo -e "  ${RED}✗${NC} $1"
}

print_info() {
    echo -e "  ${CYAN}→${NC} $1"
}

# ============================================================
# 解析命令行参数
# ============================================================
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)
            INSTALL_MODE="user"
            shift
            ;;
        --force)
            FORCE=true
            shift
            ;;
        --help)
            echo "用法: sudo $0 [选项]"
            echo ""
            echo "选项:"
            echo "  --user        卸载用户 systemd 服务"
            echo "  --force       跳过确认提示"
            echo "  --help        显示此帮助信息"
            echo ""
            echo "示例:"
            echo "  sudo $0                        # 卸载系统服务"
            echo "  sudo $0 --force                # 强制卸载系统服务"
            echo "  $0 --user                      # 卸载用户服务"
            exit 0
            ;;
        -*)
            print_err "未知选项 $1"
            echo "用法: sudo $0 [--user] [--force]"
            exit 1
            ;;
        *)
            print_err "未知参数 $1"
            echo "用法: sudo $0 [--user] [--force]"
            exit 1
            ;;
    esac
done

# ============================================================
# 显示 Banner
# ============================================================
print_banner

# ============================================================
# 权限检查
# ============================================================
print_step "1/5" "权限检查"
if [[ "$INSTALL_MODE" == "system" ]]; then
    if [[ "$EUID" -ne 0 ]]; then
        print_err "卸载系统服务需要 root 权限"
        echo ""
        echo "  请使用: sudo $0 [--user] [--force]"
        exit 1
    fi
    SERVICE_DIR="/etc/systemd/system"
    SYSTEMCTL_USER=""
    print_ok "root 权限验证通过"
else
    SERVICE_DIR="${HOME}/.config/systemd/user"
    SYSTEMCTL_USER="--user"
    print_ok "用户模式，无需 root 权限"
fi
echo ""

SERVICE_FILE="${SERVICE_DIR}/${SERVICE_NAME}.service"

# ============================================================
# 检查服务是否已安装
# ============================================================
print_step "2/5" "检查服务状态"

if [[ ! -f "$SERVICE_FILE" ]]; then
    print_warn "服务文件不存在: $SERVICE_FILE"
    print_info "服务可能尚未安装，或已被卸载。"
    echo ""
    echo -e "${CYAN}================================================${NC}"
    echo ""
    exit 0
fi

if systemctl $SYSTEMCTL_USER is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    print_info "服务当前状态: ${GREEN}运行中${NC}"
else
    print_info "服务当前状态: ${YELLOW}已停止${NC}"
fi
echo ""

# ============================================================
# 确认卸载
# ============================================================
print_step "3/5" "确认卸载"

echo -e "  ${BOLD}将执行以下操作:${NC}"
echo -e "    ${RED}1. 停止服务${NC}    systemctl $SYSTEMCTL_USER stop $SERVICE_NAME"
echo -e "    ${RED}2. 禁用服务${NC}    systemctl $SYSTEMCTL_USER disable $SERVICE_NAME"
echo -e "    ${RED}3. 删除文件${NC}    rm $SERVICE_FILE"
echo -e "    ${RED}4. 重载配置${NC}    systemctl $SYSTEMCTL_USER daemon-reload"
echo ""

if [[ "$FORCE" != true ]]; then
    echo -n -e "  ${YELLOW}?${NC} 确认卸载? [${BOLD}y${NC}/${BOLD}N${NC}] "
    read -r confirm
    case "$confirm" in
        [yY]|[yY][eE][sS])
            echo ""
            ;;
        *)
            echo ""
            print_info "操作已取消。"
            echo ""
            echo -e "${CYAN}================================================${NC}"
            echo ""
            exit 0
            ;;
    esac
else
    echo -e "  ${YELLOW}--force${NC} 模式，跳过确认"
    echo ""
fi

# ============================================================
# 停止服务
# ============================================================
print_step "4/5" "执行卸载操作"

print_info "停止服务..."
if systemctl $SYSTEMCTL_USER is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl $SYSTEMCTL_USER stop "$SERVICE_NAME"
    print_ok "服务已停止"
else
    print_info "服务未运行，跳过"
fi

print_info "禁用服务（取消开机自启）..."
if systemctl $SYSTEMCTL_USER is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
    systemctl $SYSTEMCTL_USER disable "$SERVICE_NAME"
    print_ok "服务已禁用"
else
    print_info "服务未启用，跳过"
fi

print_info "删除服务文件..."
rm -f "$SERVICE_FILE"
print_ok "服务文件已删除"

print_info "重载 systemd 配置..."
systemctl $SYSTEMCTL_USER daemon-reload
print_ok "systemd 配置已重载"
echo ""

# ============================================================
# 完成
# ============================================================
print_step "5/5" "完成"

echo ""
echo -e "${CYAN}================================================"
echo -e "  ${GREEN}${BOLD}✓  rkserver systemd 服务卸载完成${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""
echo -e "  ${BOLD}服务已从系统中完全移除。${NC}"
echo ""
