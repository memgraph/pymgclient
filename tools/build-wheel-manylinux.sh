#!/bin/bash

# This script is specifically for building wheel within the manylinux container and optionally using the static openssl and zlib.

python_version="3.14"
static_ssl=false
use_env=false
run_auditwheel=false
while [ $# -gt 0 ]; do
  case "$1" in
    --python-version)
      python_version="$2"
      shift 2
      ;;
    --static-ssl)
      static_ssl=true
      shift
      ;;
    --use-env)
      use_env=true
      shift
      ;;
    --run-auditwheel)
      run_auditwheel=true
      shift
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

echo "Building wheel for Python $python_version with static SSL: $static_ssl"

PYTHON_BINARY="python$python_version"

if $use_env; then
  $PYTHON_BINARY -m venv env
  source env/bin/activate
  pip install build auditwheel setuptools wheel
fi

args=()
if ! $static_ssl; then
  args+=("-C--build-option=build_ext" "-C--build-option=--static-openssl=False")
  OPENSSL_PREFIX=/
else
  OPENSSL_PREFIX=/opt/openssl
fi

# Treat OpenSSL/zlib headers as system headers (avoid -Werror pain)
export CFLAGS="-isystem ${OPENSSL_PREFIX}/include"
export CPPFLAGS="${CFLAGS}"

# Ensure the linker can find the static archives
export LDFLAGS="-L${OPENSSL_PREFIX}/lib -Wl,--whole-archive -lz -Wl,--no-whole-archive"


# Make CMake (mgclient) prefer your OpenSSL and use static libs
export CMAKE_PREFIX_PATH="${OPENSSL_PREFIX}"
export OPENSSL_ROOT_DIR="${OPENSSL_PREFIX}"

$PYTHON_BINARY -m build "${args[@]}"

if $run_auditwheel; then
  ARCH="$(arch)"
  cp_version=$(echo $python_version | sed 's/\.//g')
  auditwheel repair dist/*cp${cp_version}*.whl --plat manylinux_2_34_${ARCH} -w dist/ && rm dist/*linux_${ARCH}.whl
fi
