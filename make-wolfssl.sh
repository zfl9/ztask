#!/bin/bash
set -o nounset
set -o errexit
set -o pipefail

# cd the script's directory
cd "$(dirname "$0")"

version="5.9.1"
tar_file="wolfssl-$version.tar.gz"
src_dir="wolfssl-$version"
install_dir="$(pwd)/wolfssl"

install_deps() {
    if command -v apt &>/dev/null; then
        sudo apt install -y --no-upgrade gcc g++ make autoconf automake libtool wget tar
    elif command -v pacman &>/dev/null; then
        sudo pacman -Sy --noconfirm --needed gcc make autoconf automake libtool wget tar
    elif command -v dnf &>/dev/null; then
        sudo dnf install -y gcc gcc-c++ make autoconf automake libtool wget tar
    elif command -v yum &>/dev/null; then
        sudo yum install -y gcc gcc-c++ make autoconf automake libtool wget tar
    else
        echo "please ensure the dependencies are installed:"
        echo ">>> gcc g++ make autoconf automake libtool wget tar"
    fi
}

download() {
    [ -f "$tar_file" ] && return
    wget "https://github.com/wolfSSL/wolfssl/archive/refs/tags/v$version-stable.tar.gz" -O "$tar_file"
}

extract() {
    [ -d "$src_dir" ] && return
    mkdir -p "$src_dir"
    tar -xvf "$tar_file" -C "$src_dir" --strip-components=1
}

# run in subshell to avoid polluting current shell
build() (
    [ -d "$install_dir" ] && return
    cd "$src_dir"
    ./autogen.sh
    ./configure \
        --prefix="$install_dir" \
        --disable-openssl-compatible-defaults \
        --disable-opensslextra \
        --disable-oldnames \
        --enable-alpn \
        --enable-session-ticket \
        --enable-aesni \
        --enable-singlethreaded
    make && make install
)

[ -d "$install_dir" ] && exit
install_deps
download
extract
build
