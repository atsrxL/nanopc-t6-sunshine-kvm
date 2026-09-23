#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_ROOT=${RKMOON_BUILD_ROOT:-"$ROOT/build"}
PREFIX=${RKMOON_PREFIX:-"$ROOT/local"}
JOBS=${JOBS:-2}
case ${1:-offline} in
  offline)
    cmake -S "$ROOT" -B "$BUILD_ROOT/offline" -DCMAKE_BUILD_TYPE=Debug
    cmake --build "$BUILD_ROOT/offline" -j "$JOBS"
    ctest --test-dir "$BUILD_ROOT/offline" --output-on-failure
    RKMOON_NATIVE_TEST_DIR="$BUILD_ROOT/offline" PYTHONPATH="$ROOT/python" python3 -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
    ;;
  mpp)
    SRC="$ROOT/vendor/mpp"
    test "$(git -C "$SRC" rev-parse HEAD)" = 0986d01294d5c2449c14cf13af9b740368c33967
    # Private prefix only. No /usr, ldconfig, kernel, boot or service modifications.
    cmake -S "$SRC" -B "$BUILD_ROOT/mpp" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX/mpp" -DBUILD_SHARED_LIBS=ON
    cmake --build "$BUILD_ROOT/mpp" -j "$JOBS"
    cmake --install "$BUILD_ROOT/mpp"
    cmake -S "$ROOT" -B "$BUILD_ROOT/native" -DRKMOON_MPP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_PREFIX_PATH="$PREFIX/mpp" -DCMAKE_INSTALL_RPATH="$PREFIX/mpp/lib;$PREFIX/mpp/lib64"
    cmake --build "$BUILD_ROOT/native" -j "$JOBS"
    ;;
  sunshine)
    SRC="$ROOT/vendor/sunshine"
    # Must already have a reviewed/generated overlay, rather than a stock desktop binary.
    test -f "$SRC/src/rkmoon/rkmoon_bridge.cpp"
    test "$(git -C "$SRC" rev-parse HEAD)" = 63d35f702ee9e362e43263742981836ec0710384
    cmake -S "$SRC" -B "$BUILD_ROOT/sunshine" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF -DSUNSHINE_ENABLE_TRAY=OFF \
      -DSUNSHINE_ENABLE_CUDA=OFF -DCUDA_FAIL_ON_MISSING=OFF -DBOOST_USE_STATIC=OFF \
      -DSUNSHINE_ENABLE_DRM=OFF -DSUNSHINE_ENABLE_VAAPI=OFF -DSUNSHINE_ENABLE_VULKAN=OFF \
      -DSUNSHINE_ENABLE_WAYLAND=OFF -DSUNSHINE_ENABLE_X11=OFF \
      -DSUNSHINE_ENABLE_KWIN=OFF -DSUNSHINE_ENABLE_PORTAL=OFF \
      -DSUNSHINE_ASSETS_DIR_DEF="$BUILD_ROOT/sunshine/assets" \
      -DCMAKE_INSTALL_PREFIX="$PREFIX/sunshine" "${@:2}"
    cmake --build "$BUILD_ROOT/sunshine" --target sunshine -j "$JOBS"
    # Do not run upstream cmake --install: it can include system integration/permissions.
    echo "Dedicated binary: $BUILD_ROOT/sunshine/rkmoon-kvm. No web-ui target; do not install over old Sunshine."
    ;;
  *) echo 'Usage: tools/build.sh offline|mpp|sunshine [additional Sunshine -D options]' >&2;exit 2;;
esac
