#!/bin/sh
# ==========================================================================
#  package-cli.sh — package a built openmtr-cli for download
#
#  Usage: packaging/package-cli.sh <openmtr-cli binary> <os> <arch> <out dir>
#    os   : linux | macos
#    arch : amd64 | arm64
#
#  Writes into <out dir>:
#    openmtr-cli-<os>-<arch>.tar.gz          the binary + LICENSE + README
#    openmtr-cli-<os>-<arch>.tar.gz.sha256   checked by install-cli.sh
#    openmtr-cli-linux-<arch>.deb            Linux only (needs dpkg-deb and
#                                            dpkg-shlibdeps, i.e. dpkg-dev)
#
#  File names carry no version on purpose: install-cli.sh downloads them
#  from .../releases/latest/download/<name>, so a release must attach them
#  under exactly these names. The version inside comes from CMakeLists.txt.
#  Used by .github/workflows/build.yml; runs the same locally.
# ==========================================================================
set -eu

if [ $# -ne 4 ]; then
    echo "usage: $0 <openmtr-cli binary> <linux|macos> <amd64|arm64> <out dir>" >&2
    exit 2
fi
bin=$1 os=$2 arch=$3 out=$4

case "$os" in linux|macos) ;; *) echo "unknown os: $os" >&2; exit 2 ;; esac
case "$arch" in amd64|arm64) ;; *) echo "unknown arch: $arch" >&2; exit 2 ;; esac
[ -x "$bin" ] || { echo "not an executable: $bin" >&2; exit 2; }

src=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^project(OpenMTR VERSION \([0-9.]*\).*/\1/p' "$src/CMakeLists.txt")
[ -n "$version" ] || { echo "no version found in CMakeLists.txt" >&2; exit 1; }

mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi
}

# ---- tar.gz ----------------------------------------------------------------
name="openmtr-cli-$os-$arch"
mkdir -p "$work/$name"
cp "$bin" "$work/$name/openmtr-cli"
chmod 755 "$work/$name/openmtr-cli"
cp "$src/LICENSE" "$work/$name/LICENSE"
cat > "$work/$name/README.txt" <<EOF
openmtr-cli $version — OpenMTR's traceroute report on the command line.

  openmtr-cli --count 10 1.1.1.1
  openmtr-cli --count 10 --json -6 example.com
  openmtr-cli --help

Copy openmtr-cli to a directory on your PATH, e.g. /usr/local/bin.
Documentation: https://github.com/x-rated/OpenMTR#command-line-report-mode
EOF
# Owner/group of the packer are meaningless on the target; don't record them.
if tar --version 2>/dev/null | grep -q 'GNU tar'; then
    tar -C "$work" --owner=0 --group=0 -czf "$out/$name.tar.gz" "$name"
else
    tar -C "$work" --uid 0 --gid 0 -czf "$out/$name.tar.gz" "$name"
fi
(cd "$out" && sha256 "$name.tar.gz" > "$name.tar.gz.sha256")
echo "wrote $out/$name.tar.gz"

# ---- .deb (Linux) ------------------------------------------------------------
[ "$os" = linux ] || exit 0

root="$work/deb"
install -d "$root/DEBIAN" "$root/usr/bin" "$root/usr/share/doc/openmtr-cli"
install -m 755 "$bin" "$root/usr/bin/openmtr-cli"
cat > "$root/usr/share/doc/openmtr-cli/copyright" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: OpenMTR
Source: https://github.com/x-rated/OpenMTR

Files: *
Copyright: OpenMTR contributors; WinMTR Redux by White-Tiger; WinMTR (2000) by Vasile Laurentiu Stanimir
License: GPL-2
 On Debian systems, the full text of the GNU General Public License
 version 2 can be found in /usr/share/common-licenses/GPL-2.
EOF

# Library dependencies as dpkg itself computes them (glibc/libstdc++
# versions the binary really needs), so apt refuses the package on a system
# too old for it instead of installing a binary that cannot start.
mkdir -p "$work/shlibs/debian"
printf 'Source: openmtr\n\nPackage: openmtr-cli\nArchitecture: any\n' > "$work/shlibs/debian/control"
depends=$(cd "$work/shlibs" && dpkg-shlibdeps -O "$root/usr/bin/openmtr-cli" \
          | sed -n 's/^shlibs:Depends=//p')

size=$(du -sk "$root/usr" | cut -f1)
cat > "$root/DEBIAN/control" <<EOF
Package: openmtr-cli
Version: $version
Architecture: $arch
Maintainer: ${DEB_MAINTAINER:-Slamb <slamb@slamb.eu>}
Installed-Size: $size
Depends: $depends
Recommends: bind9-dnsutils | dnsutils
Section: net
Priority: optional
Homepage: https://github.com/x-rated/OpenMTR
Description: traceroute and ping report tool (OpenMTR command line)
 openmtr-cli traces the route to a host like mtr --report: it probes every
 hop for a number of cycles, then prints per-hop loss, latency, jitter,
 host names and AS numbers as a text table or JSON, and exits with a code
 scripts can act on. It needs no root: it uses unprivileged ICMP sockets
 (net.ipv4.ping_group_range). dig (bind9-dnsutils) is used for AS number
 lookups.
EOF

deb="$out/openmtr-cli-linux-$arch.deb"
dpkg-deb --root-owner-group --build "$root" "$deb" >/dev/null
(cd "$out" && sha256 "$(basename "$deb")" > "$(basename "$deb").sha256")
echo "wrote $deb"
