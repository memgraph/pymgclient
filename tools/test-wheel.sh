#!/bin/bash

# Installs a pymgclient wheel into a fresh virtual environment for each Python
# version in $TEST_PYTHON_VERSIONS and runs pytest with each of them. Every
# version is tested even if an earlier one fails; the script fails at the end
# if any version failed. On GitHub Actions the output of each version is
# grouped and a result table is written to the job summary.
#
# Usage: test-wheel.sh <wheel> [pytest args...]
#
# Each Python version is looked up as pythonX.Y on PATH (Linux/macOS) or in the
# runner tool cache (Windows, where every install is just python.exe).

wheel="$1"
shift
pytest_args=("$@")

if [[ -z "${TEST_PYTHON_VERSIONS:-}" ]]; then
  echo "TEST_PYTHON_VERSIONS is not set"
  exit 1
fi

find_python() {
  local version="$1"
  if command -v "python$version" > /dev/null 2>&1; then
    command -v "python$version"
    return 0
  fi
  if [[ -n "${RUNNER_TOOL_CACHE:-}" ]]; then
    local tool_cache="$RUNNER_TOOL_CACHE"
    if command -v cygpath > /dev/null 2>&1; then
      tool_cache="$(cygpath -u "$tool_cache")"
    fi
    # The tool cache may hold several patch releases; use the newest.
    local found
    found=$(ls -d "$tool_cache/Python/$version".*/x64/python.exe 2> /dev/null | sort -V | tail -n 1)
    if [[ -n "$found" ]]; then
      echo "$found"
      return 0
    fi
  fi
  return 1
}

test_version() {
  local version="$1"
  local python
  if ! python=$(find_python "$version"); then
    echo "Python $version not found"
    return 1
  fi

  local venv="venv-$version"
  rm -rf "$venv"
  "$python" -m venv "$venv" || return 1
  local venv_python="$venv/bin/python"
  if [[ ! -x "$venv_python" ]]; then
    venv_python="$venv/Scripts/python.exe"
  fi

  "$venv_python" --version || return 1
  "$venv_python" -m pip install --quiet --upgrade pip || return 1
  "$venv_python" -m pip install --quiet pyopenssl pytest tzdata "$wheel" || return 1
  "$venv_python" -c "import mgclient" || return 1
  "$venv_python" -m pytest "${pytest_args[@]}"
}

summary="${GITHUB_STEP_SUMMARY:-/dev/null}"
{
  echo "| Python | Result |"
  echo "| --- | --- |"
} >> "$summary"

failed=""
for version in $TEST_PYTHON_VERSIONS; do
  echo "::group::Python $version"
  if test_version "$version"; then
    result="passed"
  else
    result="FAILED"
    failed="$failed $version"
  fi
  echo "::endgroup::"
  if [[ "$result" == "FAILED" ]]; then
    echo "::error title=Python $version::Tests failed on Python $version"
  fi
  echo "| $version | $result |" >> "$summary"
done

if [[ -n "$failed" ]]; then
  echo "Tests failed on Python versions:$failed"
  exit 1
fi
echo "Tests passed on Python versions: $TEST_PYTHON_VERSIONS"
