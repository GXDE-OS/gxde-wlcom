# Maintainer: CharOfString <root@charofstring.cc>

pkgname=gxde-wlcom
pkgver=2.1.1.gxde8
pkgrel=1
pkgdesc='GXDE Wayland compositor'
arch=('x86_64' 'aarch64')
url='https://github.com/GXDE-OS/open-kylin-wlcom'
license=('GPL-1.0-or-later')
depends=(
  'cairo'
  'glib2'
  'json-c'
  'libdisplay-info'
  'libdrm'
  'libepoxy'
  'libglvnd'
  'libinput'
  'libjpeg-turbo'
  'libliftoff'
  'libpng'
  'librsvg'
  'libunwind'
  'libxcb'
  'libxkbcommon'
  'mesa'
  'openssl'
  'pango'
  'pixman'
  'seatd'
  'systemd-libs'
  'vulkan-icd-loader'
  'wayland'
  'xcb-util-errors'
  'xcb-util-renderutil'
  'xcb-util-wm'
  'xorg-xwayland'
)
makedepends=(
  'gettext'
  'git'
  'glslang'
  'hwdata'
  'meson'
  'ninja'
  'pkgconf'
  'systemd'
  'vulkan-headers'
  'wayland-protocols'
)
provides=('kylin-wayland-compositor' 'kylin-wayland-compositor-client')
conflicts=('kylin-wayland-compositor' 'kylin-wayland-compositor-client')
replaces=('kylin-wayland-compositor' 'kylin-wayland-compositor-client')

_commit='fb9b64f8a13cb465aedba8b8ac8b603fc83f5f8b'
_wlroots_commit='d315e23d3e444c0504ae3b230155180938e3ece0'
source=(
  "$pkgname::git+$url.git#commit=$_commit"
  "wlroots::git+https://github.com/GXDE-OS/open-kylin-wlroots.git#commit=$_wlroots_commit"
)
sha256sums=('SKIP' 'SKIP')

prepare() {
  rm -rf "$pkgname/subprojects/wlroots"
  ln -s "$srcdir/wlroots" "$pkgname/subprojects/wlroots"
  sed -i '/const struct wlr_fbox \*box = &options->src_box;/d' \
    "$srcdir/wlroots/render/pass.c"
  sed -i "s/'-Werror',/'-Wno-error',/" \
    "$srcdir/wlroots/meson.build"
}

build() {
  meson setup build "$pkgname" \
    --prefix=/usr \
    --buildtype=plain \
    -Dexamples=false \
    -Dukui_theme=true \
    -Dwlroots:renderers=gles2,vulkan
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir" --skip-subprojects
}
