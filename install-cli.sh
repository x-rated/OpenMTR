#!/bin/sh
# ==========================================================================
#  install-cli.sh — install openmtr-cli on Linux or macOS
#
#    curl -fsSL https://raw.githubusercontent.com/x-rated/OpenMTR/main/install-cli.sh | sh
#
#  Downloads openmtr-cli-<os>-<arch>.tar.gz from the latest GitHub release,
#  checks it against the release's .sha256 file and installs the binary.
#
#  Environment:
#    OPENMTR_VERSION  release tag to install (default: the latest release)
#    OPENMTR_BINDIR   where to install (default: /usr/local/bin, using sudo
#                     when it is not writable; ~/.local/bin without sudo)
#    OPENMTR_REPO     GitHub repository (default: x-rated/OpenMTR)
#    OPENMTR_BASE_URL download from here instead of the GitHub release (a
#                     mirror); must hold the same file names
# ==========================================================================
set -eu

repo=${OPENMTR_REPO:-x-rated/OpenMTR}
version=${OPENMTR_VERSION:-latest}

say()  { printf '%s\n' "$*"; }
fail() { printf 'install-cli.sh: %s\n' "$*" >&2; exit 1; }

# ---- Platform ----------------------------------------------------------------
case "$(uname -s)" in
    Linux)  os=linux ;;
    Darwin) os=macos ;;
    *)      fail "unsupported system $(uname -s); openmtr-cli is built for Linux and macOS (Windows: download openmtr-cli.exe from the release page)" ;;
esac
case "$(uname -m)" in
    x86_64|amd64)  arch=amd64 ;;
    aarch64|arm64) arch=arm64 ;;
    *)             fail "unsupported architecture $(uname -m)" ;;
esac
if [ "$os" = macos ] && [ "$arch" = amd64 ]; then
    # Rosetta shells report x86_64 on Apple Silicon; the arm64 binary is
    # still the right one there.
    if [ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = 1 ]; then
        arch=arm64
    else
        fail "Intel Macs are not supported; openmtr-cli is built for Apple Silicon only"
    fi
fi

asset="openmtr-cli-$os-$arch.tar.gz"
if [ -n "${OPENMTR_BASE_URL:-}" ]; then
    base=${OPENMTR_BASE_URL%/}
elif [ "$version" = latest ]; then
    base="https://github.com/$repo/releases/latest/download"
else
    base="https://github.com/$repo/releases/download/$version"
fi

# ---- Download & verify ---------------------------------------------------------
fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --retry 3 -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q -O "$2" "$1"
    else
        fail "needs curl or wget"
    fi
}

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM

say "Downloading $asset from $base..."
fetch "$base/$asset" "$tmp/$asset" || fail "could not download $base/$asset"
fetch "$base/$asset.sha256" "$tmp/$asset.sha256" || fail "could not download $base/$asset.sha256"

want=$(cut -d' ' -f1 < "$tmp/$asset.sha256")
if command -v sha256sum >/dev/null 2>&1; then
    have=$(sha256sum "$tmp/$asset" | cut -d' ' -f1)
elif command -v shasum >/dev/null 2>&1; then
    have=$(shasum -a 256 "$tmp/$asset" | cut -d' ' -f1)
else
    fail "needs sha256sum or shasum to verify the download"
fi
if [ -z "$want" ] || [ "$want" != "$have" ]; then
    fail "checksum mismatch for $asset"
fi

tar -xzf "$tmp/$asset" -C "$tmp"
bin="$tmp/openmtr-cli-$os-$arch/openmtr-cli"
[ -f "$bin" ] || fail "openmtr-cli missing from $asset"
chmod 755 "$bin"

# ---- Install -------------------------------------------------------------------
bindir=${OPENMTR_BINDIR:-}
sudo=""
if [ -z "$bindir" ]; then
    bindir=/usr/local/bin
    if [ ! -d "$bindir" ] || [ ! -w "$bindir" ]; then
        if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
            sudo=sudo
        else
            bindir="$HOME/.local/bin"
        fi
    fi
fi
$sudo mkdir -p "$bindir"
$sudo cp "$bin" "$bindir/openmtr-cli"
$sudo chmod 755 "$bindir/openmtr-cli"
if [ "$os" = macos ]; then
    # Only set by browsers, but harmless to clear.
    $sudo xattr -d com.apple.quarantine "$bindir/openmtr-cli" 2>/dev/null || true
fi

say "Installed $("$bindir/openmtr-cli" --version) to $bindir/openmtr-cli"

# ---- Hints -------------------------------------------------------------------
case ":$PATH:" in
    *":$bindir:"*) ;;
    *) say "Note: $bindir is not on your PATH; add it, or run $bindir/openmtr-cli." ;;
esac
if [ "$os" = linux ] && [ -r /proc/sys/net/ipv4/ping_group_range ]; then
    # Unprivileged ICMP sockets: the caller's group must be inside the range.
    # cat, not `read <`: dash reads byte by byte, which this /proc file
    # does not support.
    range=$(cat /proc/sys/net/ipv4/ping_group_range 2>/dev/null || true)
    lo=${range%%[[:space:]]*}
    hi=${range##*[[:space:]]}
    gid=$(id -g)
    if [ -n "$range" ] && { [ "$gid" -lt "$lo" ] || [ "$gid" -gt "$hi" ]; }; then
        say "Note: unprivileged ping sockets are disabled for your group; allow them with:"
        say "  sudo sysctl -w net.ipv4.ping_group_range=\"0 2147483647\""
    fi
fi
if ! command -v dig >/dev/null 2>&1; then
    say "Note: dig is not installed, so the ASN column will stay empty (install bind9-dnsutils / bind-utils)."
fi
say "Try: openmtr-cli --count 10 1.1.1.1"
