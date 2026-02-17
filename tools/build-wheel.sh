#!/bin/bash

python_version="3.14"
static_ssl=false
use_env=false
run_auditwheel=false
libc_version="2_39"
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
    --libc-version)
      libc_version="$2"
      shift 2
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

echo "Building wheel for Python $python_version with static SSL: $static_ssl and libc version: $libc_version"

PYTHON_BINARY="python$python_version"

if $use_env; then
  $PYTHON_BINARY -m venv env
  source env/bin/activate
  pip install build auditwheel setuptools wheel
fi

args=()
if ! $static_ssl; then
  args+=("-C--build-option=build_ext" "-C--build-option=--static-openssl=False")
fi

$PYTHON_BINARY -m build "${args[@]}"

if $run_auditwheel; then
  ARCH="$(arch)"
  cp_version=$(echo $python_version | sed 's/\.//g')
  auditwheel repair dist/*cp${cp_version}*.whl --plat manylinux_${libc_version}_${ARCH} -w dist/ && rm dist/*linux_${ARCH}.whl
fi
