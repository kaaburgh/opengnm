#!/bin/bash
# build.sh — Build opengnm using Docker (OpenOrbis SDK)
#
# Usage:
#   ./build.sh          — build PS4 library only (orbis target)
#   ./build.sh lib      — build PS4 library only (orbis target)
#   ./build.sh headers  — install headers only (no compilation)
#   ./build.sh tests    — build + run host tests (generic target)
#   ./build.sh docker-build       — OpenOrbis Docker build + link smoke
#   ./build.sh docker-link-smoke  — OpenOrbis Docker link smoke only
#   ./build.sh docker-hardware-smoke — build PS4 hardware smoke ELF
#   ./build.sh clean    — clean build directory
#   ./build.sh shell    — open shell in Docker with SDK

set -e

IMAGE="openorbisofficial/toolchain:latest"
DOCKER_OO_PS4_TOOLCHAIN="${DOCKER_OO_PS4_TOOLCHAIN:-/usr/lib/OpenOrbisSDK}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OPENGNM_DIR="$SCRIPT_DIR"

# Write generic config.mak on host
write_generic_config() {
    cat > "$OPENGNM_DIR/config.mak" << 'EOF'
DESTDIR=/usr/local
BINDIR=/bin
INCDIR=/include
LIBDIR=/lib
LIBEXT=.so
LIBVER=0.1.0
PLATFORM=generic
AR=ar
CC=clang
LD=clang
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -I./include -I./src -O2 -g
LDFLAGS=-L. -lopengnm -lm
LIB_LDFLAGS=-shared -L. -lm
EOF
}

# Write orbis config.mak on host
write_orbis_config() {
    cat > "$OPENGNM_DIR/config.mak" << 'EOF'
DESTDIR=/usr/local
BINDIR=/bin
INCDIR=/include
LIBDIR=/lib
LIBEXT=.so
LIBVER=0.1.0
PLATFORM=orbis
TOOLCHAIN=$(OO_PS4_TOOLCHAIN)
AR=ar
CC=clang
LD=ld.lld
CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -I./include -I./src -O2 -g --target=x86_64-ps4-elf -fPIC -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
LDFLAGS=-m elf_x86_64 -L$(TOOLCHAIN)/lib -lc -lkernel -lSceGnmDriver -lSceVideoOut -L. -lopengnm
LIB_LDFLAGS=-shared -m elf_x86_64 -L$(TOOLCHAIN)/lib -lc -lkernel -lSceGnmDriver -lSceVideoOut -L. -lopengnm
EOF
}

# Default action
ACTION="${1:-all}"

case "$ACTION" in
    all|lib)
        write_orbis_config
        make lib
        echo "opengnm: PS4 library built (orbis target)"
        ;;
    headers)
        write_orbis_config
        make install-headers
        echo "opengnm: Headers installed"
        ;;
    tests)
        write_generic_config
        make tests
        ;;
    clean)
        make clean
        ;;
    shell)
        docker run --rm -it -v "$OPENGNM_DIR:/opengnm" -w /opengnm \
            -e OO_PS4_TOOLCHAIN="$DOCKER_OO_PS4_TOOLCHAIN" \
            "$IMAGE" /bin/bash
        ;;
    docker-build)
        write_orbis_config
        docker run --rm -v "$OPENGNM_DIR:/opengnm" -w /opengnm \
            -e OO_PS4_TOOLCHAIN="$DOCKER_OO_PS4_TOOLCHAIN" \
            "$IMAGE" /bin/bash -c "make link-smoke hardware-smoke install-lib DESTDIR=/tmp/opengnm-install"
        ;;
    docker-link-smoke)
        write_orbis_config
        docker run --rm -v "$OPENGNM_DIR:/opengnm" -w /opengnm \
            -e OO_PS4_TOOLCHAIN="$DOCKER_OO_PS4_TOOLCHAIN" \
            "$IMAGE" /bin/bash -c "make link-smoke"
        ;;
    docker-hardware-smoke)
        write_orbis_config
        docker run --rm -v "$OPENGNM_DIR:/opengnm" -w /opengnm \
            -e OO_PS4_TOOLCHAIN="$DOCKER_OO_PS4_TOOLCHAIN" \
            "$IMAGE" /bin/bash -c "make hardware-smoke"
        ;;
    *)
        echo "Usage: $0 {all|lib|headers|tests|clean|shell|docker-build|docker-link-smoke|docker-hardware-smoke}"
        exit 1
        ;;
esac
