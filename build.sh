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
#   ./build.sh docker-hardware-pkg — build PS4 hardware smoke package
#   ./build.sh stage-hardware-pkg  — build + upload hardware smoke package
#   ./build.sh clean    — clean build directory
#   ./build.sh shell    — open shell in Docker with SDK

set -e

IMAGE="openorbisofficial/toolchain:latest"
DOCKER_OO_PS4_TOOLCHAIN="${DOCKER_OO_PS4_TOOLCHAIN:-/usr/lib/OpenOrbisSDK}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OPENGNM_DIR="$SCRIPT_DIR"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CREATE_FSELF_SRC="${CREATE_FSELF_SRC:-$ROOT_DIR/../OpenOrbis/create-fself}"
CREATE_GP4_SRC="${CREATE_GP4_SRC:-$ROOT_DIR/../OpenOrbis/create-gp4}"
TMP_DIR=""

require_go() {
    if ! command -v go >/dev/null 2>&1; then
        echo "Go is required to build OpenOrbis package helper tools." >&2
        exit 1
    fi
}

ensure_tmp_dir() {
    if [[ -z "$TMP_DIR" ]]; then
        TMP_DIR="$(mktemp -d)"
        trap '[[ -z "$TMP_DIR" ]] || rm -rf "$TMP_DIR"' EXIT
    fi
}

prepare_package_helpers() {
    if [[ -z "${CREATE_FSELF_BIN:-}" && -d "$CREATE_FSELF_SRC/cmd/create-fself" ]]; then
        require_go
        ensure_tmp_dir
        (
            cd "$CREATE_FSELF_SRC/cmd/create-fself"
            GOOS=linux GOARCH=amd64 go build -modfile=go-linux.mod -o "$TMP_DIR/create-fself"
        )
        CREATE_FSELF_BIN="$TMP_DIR/create-fself"
    fi

    if [[ -z "${CREATE_GP4_BIN:-}" && -d "$CREATE_GP4_SRC/cmd/create-gp4" ]]; then
        require_go
        ensure_tmp_dir
        (
            cd "$CREATE_GP4_SRC/cmd/create-gp4"
            GOOS=linux GOARCH=amd64 go build -o "$TMP_DIR/create-gp4"
        )
        CREATE_GP4_BIN="$TMP_DIR/create-gp4"
    fi
}

run_docker_package_make() {
    prepare_package_helpers
    docker_args=(
        --rm
        --platform linux/amd64
        -v "$ROOT_DIR:/work"
        -w /work/opengnm
        -e OO_PS4_TOOLCHAIN="$DOCKER_OO_PS4_TOOLCHAIN"
    )

    if [[ -n "${CREATE_FSELF_BIN:-}" ]]; then
        docker_args+=(
            -v "$CREATE_FSELF_BIN:/usr/local/bin/create-fself:ro"
            -e CREATE_FSELF=/usr/local/bin/create-fself
        )
    fi

    if [[ -n "${CREATE_GP4_BIN:-}" ]]; then
        docker_args+=(
            -v "$CREATE_GP4_BIN:/usr/local/bin/create-gp4:ro"
            -e CREATE_GP4=/usr/local/bin/create-gp4
        )
    fi

    docker run "${docker_args[@]}" "$IMAGE" /bin/bash -lc \
        "make clean >/dev/null 2>&1 || true; make hardware-smoke-pkg"
}

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
    docker-hardware-pkg)
        write_orbis_config
        run_docker_package_make
        ;;
    stage-hardware-pkg)
        "$OPENGNM_DIR/stage_hw_smoke_pkg.sh"
        ;;
    *)
        echo "Usage: $0 {all|lib|headers|tests|clean|shell|docker-build|docker-link-smoke|docker-hardware-smoke|docker-hardware-pkg|stage-hardware-pkg}"
        exit 1
        ;;
esac
