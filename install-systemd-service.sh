#!/bin/bash
#
# RKServer - systemd 服务安装脚本
#
# 用法:
#   sudo ./install-systemd-service.sh                    # 使用默认路径安装
#   sudo ./install-systemd-service.sh /opt/RKServer      # 指定可执行文件路径
#   sudo ./install-systemd-service.sh --user              # 安装为用户服务（无需 root）
#
# 选项:
#   --user        安装为用户 systemd 服务（~/.config/systemd/user/）
#   --help        显示此帮助信息

set -euo pipefail

SERVICE_NAME="RKServer"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
DEFAULT_EXEC_PATH="${PROJECT_DIR}/build/RKServer"
EXEC_PATH=""
INSTALL_MODE="system"  # system 或 user

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
    echo -e "  systemd 服务安装脚本 | ${BOLD}${mode_label}${NC}"
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
        --help)
            echo "用法: sudo $0 [选项] [可执行文件路径]"
            echo ""
            echo "选项:"
            echo "  --user        安装为用户 systemd 服务（无需 root）"
            echo "  --help        显示此帮助信息"
            echo ""
            echo "参数:"
            echo "  可执行文件路径  指定 RKServer 可执行文件路径（可选）"
            echo ""
            echo "示例:"
            echo "  sudo $0                              # 使用默认路径安装系统服务"
            echo "  sudo $0 /opt/RKServer/bin/RKServer   # 指定路径安装系统服务"
            echo "  $0 --user                            # 安装为用户服务"
            exit 0
            ;;
        -*)
            print_err "未知选项 $1"
            echo "用法: sudo $0 [--user] [可执行文件路径]"
            exit 1
            ;;
        *)
            EXEC_PATH="$1"
            shift
            ;;
    esac
done

# 若未指定路径，使用默认路径
if [[ -z "$EXEC_PATH" ]]; then
    EXEC_PATH="$DEFAULT_EXEC_PATH"
fi

# ============================================================
# 显示 Banner
# ============================================================
print_banner

# ============================================================
# 权限检查
# ============================================================
print_step "1/7" "权限检查"
if [[ "$INSTALL_MODE" == "system" ]]; then
    if [[ "$EUID" -ne 0 ]]; then
        print_err "安装系统服务需要 root 权限"
        echo ""
        echo "  请使用: sudo $0 [--user] [可执行文件路径]"
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
# 检查可执行文件
# ============================================================
print_step "2/7" "检查可执行文件"

if [[ ! -f "$EXEC_PATH" ]]; then
    print_err "找不到可执行文件: $EXEC_PATH"
    echo ""
    echo "  请先编译项目:"
    echo "    cd ${PROJECT_DIR}"
    echo "    cmake -B build"
    echo "    cmake --build build -j\$(nproc)"
    exit 1
fi

if [[ ! -x "$EXEC_PATH" ]]; then
    print_warn "文件没有执行权限，正在设置..."
    chmod +x "$EXEC_PATH"
fi

# 转换为绝对路径
EXEC_PATH="$(cd "$(dirname "$EXEC_PATH")" && pwd)/$(basename "$EXEC_PATH")"
print_ok "可执行文件: ${BOLD}$EXEC_PATH${NC}"
echo ""

# ============================================================
# 确定运行用户和组
# ============================================================
print_step "3/7" "配置运行用户"

if [[ "$INSTALL_MODE" == "system" ]]; then
    # 尝试使用普通用户运行（更安全），如果存在则使用
    if [[ -n "${SUDO_USER:-}" ]]; then
        RUN_USER="$SUDO_USER"
    elif [[ -n "${USER:-}" ]]; then
        RUN_USER="$USER"
    else
        RUN_USER="nobody"
    fi
    RUN_GROUP=$(id -gn "$RUN_USER" 2>/dev/null || echo "nobody")
else
    RUN_USER="$USER"
    RUN_GROUP=$(id -gn)
fi

print_ok "运行用户: ${BOLD}$RUN_USER${NC}"
print_ok "运行组:   ${BOLD}$RUN_GROUP${NC}"
echo ""

# ============================================================
# 创建服务目录
# ============================================================
print_step "4/7" "创建服务目录"

if [[ "$INSTALL_MODE" == "user" ]]; then
    mkdir -p "$SERVICE_DIR"
    print_ok "用户服务目录: ${BOLD}$SERVICE_DIR${NC}"
else
    print_ok "系统服务目录: ${BOLD}$SERVICE_DIR${NC}"
fi
echo ""

# ============================================================
# 生成服务文件
# ============================================================
print_step "5/7" "生成 systemd 服务文件"

cat > "$SERVICE_FILE" << EOF
[Unit]
Description=RKServer - RK Server (Rockchip NPU LLM Inference)
Documentation=https://github.com/rockchip-linux/RKServer
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=${RUN_USER}
Group=${RUN_GROUP}
WorkingDirectory=${PROJECT_DIR}
Environment=LD_LIBRARY_PATH=${PROJECT_DIR}/lib:/usr/lib64:\$LD_LIBRARY_PATH
Environment=OMP_NUM_THREADS=4
Environment=RKNPU2DRV_LOG_LEVEL=2
ExecStart=${EXEC_PATH}
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
SyslogIdentifier=RKServer

# 安全加固
NoNewPrivileges=true
ProtectSystem=full
ProtectHome=false
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF

print_ok "服务文件已创建: ${BOLD}$SERVICE_FILE${NC}"
echo ""

# ============================================================
# 重载 systemd 并启用服务
# ============================================================
print_step "6/7" "注册并启动服务"

print_info "重载 systemd 配置..."
systemctl $SYSTEMCTL_USER daemon-reload
print_ok "systemd 配置已重载"

print_info "启用服务（开机自启）..."
systemctl $SYSTEMCTL_USER enable "$SERVICE_NAME"
print_ok "服务已启用"

print_info "启动服务..."
systemctl $SYSTEMCTL_USER start "$SERVICE_NAME"
print_ok "服务已启动"
echo ""

# ============================================================
# 检查服务状态
# ============================================================
print_step "7/7" "验证服务状态"

sleep 1
if systemctl $SYSTEMCTL_USER is-active --quiet "$SERVICE_NAME"; then
    print_ok "服务正在运行"
else
    print_warn "服务可能未成功启动，请检查日志:"
    echo ""
    systemctl $SYSTEMCTL_USER status "$SERVICE_NAME" --no-pager -l
    echo ""
    print_info "查看完整日志: journalctl $SYSTEMCTL_USER -u $SERVICE_NAME -n 50 --no-pager"
fi
echo ""

# ============================================================
# 完成
# ============================================================
echo -e "${CYAN}================================================"
echo -e "  ${GREEN}${BOLD}✓  RKServer systemd 服务安装完成${NC}"
echo -e "${CYAN}================================================${NC}"
echo ""
echo -e "  ${BOLD}常用命令:${NC}"
echo -e "    ${GREEN}查看状态${NC}    systemctl $SYSTEMCTL_USER status $SERVICE_NAME"
echo -e "    ${GREEN}启动服务${NC}    systemctl $SYSTEMCTL_USER start $SERVICE_NAME"
echo -e "    ${GREEN}停止服务${NC}    systemctl $SYSTEMCTL_USER stop $SERVICE_NAME"
echo -e "    ${GREEN}重启服务${NC}    systemctl $SYSTEMCTL_USER restart $SERVICE_NAME"
echo -e "    ${GREEN}查看日志${NC}    journalctl $SYSTEMCTL_USER -u $SERVICE_NAME -f"
echo ""
if [[ "$INSTALL_MODE" == "system" ]]; then
    echo -e "  ${BOLD}提示:${NC} 服务已设置为开机自启。"
fi
echo -e "  ${BOLD}配置文件:${NC}   ${PROJECT_DIR}/config.json"
echo -e "  ${BOLD}日志文件:${NC}   ${PROJECT_DIR}/server.log"
echo ""
