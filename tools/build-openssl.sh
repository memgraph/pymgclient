#!/bin/bash

OPENSSL_VERSION="3.5.4"
ZLIB_VERSION="1.3.1"
OUT_DIR="/opt/openssl"
PYTHON_VERSION="3.12"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --openssl-version)
            OPENSSL_VERSION=$2
            shift 2
        ;;
        --zlib-version)
            ZLIB_VERSION=$2
            shift 2
        ;;
        --out-dir)
            OUT_DIR=$2
            shift 2
        ;;
        --python-version)
            PYTHON_VERSION=$2
            shift 2
        ;;
        *)
            echo "Error: Unknown option '$1'"
            exit 1
        ;;
    esac
done


# check if conan is installed
if ! command -v conan &> /dev/null; then
    python${PYTHON_VERSION} -m pip install conan==2.24.0
fi

# check if a conan profile exists
if [ ! -f "$HOME/.conan2/profiles/default" ]; then
    echo "Creating conan profile"
    conan profile detect
fi

if [[ -n "$CONAN_REMOTE" ]]; then
    conan remote add artifactory $CONAN_REMOTE --force
fi

conan install   \
  --lockfile="" \
  --requires=openssl/$OPENSSL_VERSION  \
  --requires=zlib/$ZLIB_VERSION \
  --build=missing \
  -o openssl/*:shared=False \
  -o zlib/*:shared=False \
  --deployer=full_deploy \
  --output-folder=$OUT_DIR

# move the lib, bin, include, and lib directories to the out dir
mv -vf $OUT_DIR/full_deploy/host/openssl/$OPENSSL_VERSION/Release/$(arch)/* $OUT_DIR
# Copy zlib (merges directories), then remove source
cp -r $OUT_DIR/full_deploy/host/zlib/$ZLIB_VERSION/Release/$(arch)/* $OUT_DIR/ && \
rm -rf $OUT_DIR/full_deploy/host/zlib/$ZLIB_VERSION/Release/$(arch)/*
