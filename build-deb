#!/bin/bash

# Copyright (C) 2026 CharOfString
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# 此脚本用于在调试时生成安装包，便于在调试机器上轻松部署与卸载。
#
# 与旧模板不同，本脚本「化简」了构建流程，所有项目相关信息都从 debian/ 自动推导，
# 通常无需逐项目改脚本：
#   - 构建依赖：从 debian/control 的 Build-Depends 自动安装（apt build-dep），不再手写包名；
#   - 包名/产物名：从 debian/changelog 自动取得，清理时据此删除上级目录的产物；
#   - 构建系统（cmake/meson/…）：由 debian/rules 决定，脚本不关心。
#
# 用法: ./build-deb <选项>
#
# 选项:
#    -b, --binary          仅构建二进制包（默认行为）
#    -d, --install-deps    先安装构建依赖(读 debian/control)，再构建
#    -c, --clean           仅清理构建产物后退出
#    -h, --help            打印帮助信息

set -o pipefail

# 路径配置：本脚本位于项目根目录(与 debian/ 同级)，故项目根 = 脚本所在目录。
CUR_SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJ_ROOT="$CUR_SCRIPT_DIR"

# 参数
BUILD_BIN=true
INSTALL_DEPS=false
ACT_CLEANUPS=false

# 包名(从 debian/changelog 推导，用于清理上级目录的产物)
PKG_NAME=""

# 打印帮助
print_help() {
    echo "使用方法: $0 <选项>"
    echo ""
    echo "可用选项:"
    echo "  -b, --binary          仅构建二进制包(默认行为)"
    echo "  -d, --install-deps    先按 debian/control 安装构建依赖，再构建二进制包"
    echo "  -c, --clean           清理构建产物后退出"
    echo "  -h, --help            打印此信息"
    echo ""
    echo "用法示例:"
    echo "  $0 -d                # 装齐构建依赖并构建"
    echo "  $0                   # 直接构建(依赖已就绪)"
    echo "  $0 -c                # 清理构建产物"
    exit 0
}

# 解析参数
parse_args() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -b|--binary)
                BUILD_BIN=true
                shift
                ;;
            -c|--clean)
                ACT_CLEANUPS=true
                shift
                ;;
            -d|--install-deps)
                INSTALL_DEPS=true
                BUILD_BIN=true
                shift
                ;;
            -h|--help)
                print_help
                ;;
            *)
                echo "未知选项: $1"
                echo "以下文档可能有帮助: "
                print_help
                ;;
        esac
    done
}

# Util: 检查命令是否存在
is_cmd_exists() {
    command -v "$1" &> /dev/null
}

# 校验项目结构 + 取包名
detect_project() {
    if [[ ! -f "$PROJ_ROOT/debian/control" || ! -f "$PROJ_ROOT/debian/changelog" ]]; then
        echo "错误: 在 $PROJ_ROOT 下未找到 debian/control 或 debian/changelog。"
        echo "本脚本需与 debian/ 同级放在项目根目录。"
        exit 1
    fi
    PKG_NAME="$(cd "$PROJ_ROOT" && dpkg-parsechangelog -S Source 2>/dev/null)"
    if [[ -z "$PKG_NAME" ]]; then
        echo "错误: 无法从 debian/changelog 解析源码包名。"
        exit 1
    fi
    echo "项目根目录: $PROJ_ROOT"
    echo "源码包名:   $PKG_NAME"
}

# 检查最基本的工具链(其余构建工具由 debian/control 的 Build-Depends 提供)
check_toolchains() {
    echo "正在检查基础工具链，请稍候..."
    local missing_tools=()

    # dpkg-buildpackage / dpkg-parsechangelog 均来自 dpkg-dev
    if ! is_cmd_exists "dpkg-buildpackage"; then
        missing_tools+=("dpkg-dev")
    fi
    # apt-get build-dep 用于自动装构建依赖
    if ! is_cmd_exists "apt-get"; then
        missing_tools+=("apt")
    fi

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        echo "缺少以下基础工具: ${missing_tools[*]}"
        echo "可以尝试运行 sudo apt install ${missing_tools[*]}"
        exit 1
    fi
    echo "基础工具链检查通过！"
}

# 安装构建依赖：直接读 debian/control 的 Build-Depends，不再手写包名。
auto_install_deps() {
    if [[ "$INSTALL_DEPS" != true ]]; then
        return
    fi
    echo "准备按 debian/control 安装构建依赖，请稍候..."
    sudo apt-get update

    echo ""
    echo ">>> 注意：以下 apt 操作未加 -y，请仔细核对「将要安装 / 将要卸载」，"
    echo ">>>       确认不会有任何 GXDE 系统包被冲突卸载！！"
    echo ""
    # apt-get build-dep 直接解析本目录 debian/control 的 Build-Depends
    sudo apt-get build-dep "$PROJ_ROOT"
    echo "构建依赖安装完成。"
}

# 预取 meson 子项目
# deb 构建时 dh 的 meson 后端会加 --wrap-mode=nodownload(Debian 策略:构建过程不联网),
# 此时 meson 无法再去拉 wrap 子项目。故在调用 dpkg-buildpackage 之前(此阶段允许联网)
# 先把 subprojects/*.wrap 拉成本地目录;已存在的子项目会被 meson 跳过。
prefetch_subprojects() {
    [[ -d subprojects ]] || return 0
    shopt -s nullglob
    local wraps=(subprojects/*.wrap)
    shopt -u nullglob
    [[ ${#wraps[@]} -eq 0 ]] && return 0

    if ! is_cmd_exists meson; then
        echo "警告: 未找到 meson，跳过子项目预取(若子项目已 vendored 则可忽略)。"
        return 0
    fi
    echo "预取 meson 子项目(wrap → subprojects/，已存在的会跳过)..."
    if ! meson subprojects download; then
        echo "错误: meson subprojects download 失败(检查网络 / git / wrap 配置)。"
        exit 1
    fi
}

# 清理构建产物
exec_clean() {
    echo "执行清理..."
    cd "$PROJ_ROOT" || exit 1

    # 让 debhelper 清理 debian/ 下的中间产物(它知道有哪些二进制包)
    if [[ -f debian/rules ]]; then
        fakeroot debian/rules clean >/dev/null 2>&1 || dh clean >/dev/null 2>&1 || true
    fi

    # 兜底清理常见中间目录/文件
    rm -rf debian/.debhelper/ debian/tmp/
    rm -f debian/files debian/*.substvars debian/*.debhelper.log debian/debhelper-build-stamp
    rm -rf obj-*-linux-gnu/ obj-*/

    # 上级目录的产物(deb/buildinfo/changes/dsc/源码 tar)，按包名匹配
    # 注意用 ${PKG_NAME}* 以同时覆盖同源的子包(如 foo / foo-client)
    if [[ -n "$PKG_NAME" ]]; then
        rm -f ../"${PKG_NAME}"*_*.deb \
              ../"${PKG_NAME}"_*.buildinfo \
              ../"${PKG_NAME}"_*.changes \
              ../"${PKG_NAME}"_*.dsc \
              ../"${PKG_NAME}"_*.tar.* 2>/dev/null
    fi

    echo "清理完成！"
}

# 构建
exec_build() {
    local dpkg_args=("-us" "-uc")
    [[ "$BUILD_BIN" == true ]] && dpkg_args+=("-b")

    cd "$PROJ_ROOT" || exit 1
    prefetch_subprojects
    echo "开始使用 dpkg-buildpackage 构建&打包(参数: ${dpkg_args[*]})..."
    dpkg-buildpackage "${dpkg_args[@]}"

    echo "构建完成。Deb 包应位于上级目录:"
    ls -1 ../"${PKG_NAME}"*_*.deb 2>/dev/null || echo "  (未找到 .deb，请检查上面的构建输出)"
}

# 主脚本
echo "========================================="
echo "      GXDE DEB 构建脚本 REL 1.0.0"
echo "========================================="
echo ""

parse_args "$@"
detect_project

if [[ "$ACT_CLEANUPS" == true ]]; then
    exec_clean
    exit 0
fi

check_toolchains
auto_install_deps
exec_build
