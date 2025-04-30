#!/bin/bash

# This script is for installing the dependencies required for building,
# installing and testing pymgclient

#!/usr/bin/env bash
set -euo pipefail

# defaults
python_version=""
distro=""

usage() {
  cat <<EOF
Usage: $0 [<distro>] [--python-version X.Y]
  <distro>               Optional distro tag, e.g. ubuntu-24.04, fedora-42
  --python-version X.Y   Optional Python MAJOR.MINOR (e.g. 3.12).
                         Defaults to system python3's MAJOR.MINOR.
EOF
  exit 1
}


# parse flags + optional positional distro
while [[ $# -gt 0 ]]; do
  case "$1" in
    --python-version)
      if [[ -z "${2-}" ]]; then
        echo "Error: --python-version needs an argument" >&2
        usage
      fi
      python_version="$2"
      shift 2
      ;;
    -h|--help)
      usage
      ;;
    --*)  # any other flag
      echo "Unknown option: $1" >&2
      usage
      ;;
    *)    # first non-flag is our distro
      if [[ -z "$distro" ]]; then
        distro="$1"
        shift
      else
        echo "Unexpected argument: $1" >&2
        usage
      fi
      ;;
  esac
done


# detect python_version if not given
if [[ -z "$python_version" ]]; then
  python_version="$(python3 --version | grep -Eo '[0-9]+\.[0-9]+' )"
fi
python_binary="python${python_version}"

# Detect distro from /etc/os-release
detect_distro() {
  if [[ -r /etc/os-release ]]; then
    . /etc/os-release
    # Normalize common IDs
    case "$ID" in
      ubuntu|debian|linuxmint|fedora|centos|rhel|rocky)
        # version might be "24.04" or "9" or "42"
        # some versions include quotes
        ver="${VERSION_ID//\"/}"
        echo "${ID} ${ver}"
        ;;
      *)
        echo "unknown-$(uname -s | tr '[:upper:]' '[:lower:]') $(uname -r)" ;;
    esac
  else
    echo "unknown-$(uname -s | tr '[:upper:]' '[:lower:]') $(uname -r)"
  fi
}

# Ensure at least one arg
if [[ $# -gt 1 ]]; then
  usage
fi

# If distro not provided, detect it
if [[ -z "$distro" ]]; then
  read distro version < <(detect_distro)
  if [[ "$distro" == unknown* ]]; then
    echo "Unknown distro detected"
    exit 1
  fi
else
  # split version from distro, e.g. `ubuntu-24.04` -> `ubuntu` `24.04`
  version="${distro#*-}"
  distro="${distro%%-*}"
fi
echo "Linux Distro: $distro $version"

# detect if we need sudo or not
SUDO=()
if (( EUID != 0 )); then
  if ! command -v sudo &>/dev/null; then
    echo "Error: root privileges or sudo required." >&2
    exit 1
  fi
  SUDO=(sudo)
fi

DEB_DEPS=(
  python${python_version}
  python3-pip
  python3-setuptools
  python3-wheel
  libpython${python_version}
  cmake
  g++
  libssl-dev
  netcat-traditional
)

RPM_DEPS=(
  python${python_version}
  python3-pip
  python3-setuptools
  python3-wheel
  python3-devel
  cmake
  g++
  openssl-devel
  nmap-ncat
)

install_deb() {
  echo "Installing DEB dependencies..."
  installed_python_version="$(( python3 --version 2>&1 || echo ) | grep -Po '(?<=Python )\d+\.\d+' || true)"
  if [[ "$python_version" != "$installed_python_version" ]]; then
    echo "Installed Python version ${installed_python_version} does not match requested version ${python_version}"
    if [[ "$distro" == "debian" ]]; then
      exit 1
    else
      echo "Adding deadsnakes PPA"
      "${SUDO[@]}" apt-get update
      "${SUDO[@]}" apt-get install -y software-properties-common 
      "${SUDO[@]}" add-apt-repository -y ppa:deadsnakes/ppa
    fi
  fi
  if [[ ("$distro" == "ubuntu" && ${version#*.} -ge 24)  \
    || ("$distro" == "linuxmint" && ${version#*.} -ge 22) ]]; then
    DEB_DEPS+=( libcurl4t64 )
  else
    DEB_DEPS+=( libcurl4 )
  fi
  "${SUDO[@]}" apt-get update
  "${SUDO[@]}" apt-get install -y ${DEB_DEPS[*]}
}

install_rpm() {
  echo "Installing RPM dependencies..."
  "${SUDO[@]}" dnf install -y ${RPM_DEPS[*]}
}

case "$distro" in
  debian|ubuntu|linuxmint)
    install_deb
    ;;
  centos|fedora|rocky|rhel)
    install_rpm
    ;;
  *)
    echo "Unsupported Distro: $distro" >&2
    exit 1
    ;;
esac

# install python dependencies
export PIP_BREAK_SYSTEM_PACKAGES=1
pkgs=( networkx pytest pyopenssl sphinx )
for pkg in "${pkgs[@]}"; do
  echo "Installing/upgrading $pkg..."
  if ! "$python_binary" -m pip install --upgrade "$pkg"; then
    echo "Warning: pip failed on $pkg, continuing…" >&2
  fi
done