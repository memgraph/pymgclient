# Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import platform
import shutil
import sys
import configparser
from distutils import log
from distutils.core import DistutilsExecError, DistutilsPlatformError
from pathlib import Path
from typing import List

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

IS_WINDOWS = sys.platform == "win32"
IS_APPLE = sys.platform == "darwin"
IS_X64 = platform.architecture()[0] == "64bit"

if IS_WINDOWS:
    # https://stackoverflow.com/a/57109148/6639989
    import distutils.cygwinccompiler

    distutils.cygwinccompiler.get_msvcr = lambda: []

with open("README.md", "r") as fh:
    readme = fh.read()
    long_description = "\n".join(readme.split("\n")[2:]).lstrip()

# Throughout this file "mgclient" can mean two different things:
# 1. The mgclient library which is the official Memgraph client library.
# 2. The mgclient python extension module which is a wrapper around the
#    client library.
EXTENSION_NAME = "mgclient"

sources = [str(path) for path in Path("src").glob("*.c")]

headers = [str(path) for path in Path("src").glob("*.h")]

parser = configparser.ConfigParser()
parser.read("setup.cfg")

static_openssl = parser.getboolean("build_ext", "static_openssl", fallback=False)


def list_all_files_in_dir(path):
    result = []
    for root, _dirs, files in os.walk(path):
        result.extend(os.path.join(root, f) for f in files)

    return result


class BuildMgclientExt(build_ext):
    """
    Builds using cmake instead of the python setuptools implicit build
    """

    user_options = build_ext.user_options[:]
    user_options.append(("static-openssl=", None, "Compile with statically linked OpenSSL."))

    boolean_options = build_ext.boolean_options[:]
    boolean_options.append(("static-openssl"))

    def initialize_options(self):
        build_ext.initialize_options(self)
        self.static_openssl = static_openssl

    def run(self):
        """
        Perform build_cmake before doing the 'normal' stuff
        """
        self.announce(f"Using static OpenSSL: {bool(self.static_openssl)}", level=log.INFO)

        for extension in self.extensions:
            if extension.name == EXTENSION_NAME:
                self.build_mgclient_for(extension)

        if IS_WINDOWS:
            if self.compiler is None:
                self.compiler = "mingw32"
            elif self.compiler != "mingw32":
                raise DistutilsPlatformError(
                    f"The specified compiler {self.compiler} is not supported on windows, only mingw32 is supported."
                )

        super().run()

    def get_cmake_binary(self):
        cmake_env_var_name = "PYMGCLIENT_CMAKE"
        custom_cmake = os.getenv(cmake_env_var_name)
        if custom_cmake is None:
            # cmake3 is checked before cmake, because on CentOS cmake refers
            # to CMake 2.*
            for possible_cmake in ["cmake3", "cmake"]:
                self.announce(f"Checking if {possible_cmake} can be used", level=log.INFO)

                which_cmake = shutil.which(possible_cmake)
                if which_cmake is not None:
                    self.announce(f"Using {which_cmake}", level=log.INFO)
                    return os.path.abspath(which_cmake)

                self.announce(f"{possible_cmake} is not accesible", level=log.INFO)
            raise DistutilsExecError("Cannot found suitable cmake")
        else:
            self.announce(
                f"Using the value of {cmake_env_var_name} for CMake, which is" f"{custom_cmake}", level=log.INFO
            )
            return custom_cmake

    def get_openssl_root_dir(self):
        if not IS_APPLE:
            return None

        openssl_root_dir_env_var = "OPENSSL_ROOT_DIR"
        openssl_root_dir = os.getenv(openssl_root_dir_env_var)

        if openssl_root_dir:
            self.announce(
                f"Using the value of {openssl_root_dir_env_var} for OpenSSL," f"which is {openssl_root_dir}",
                level=log.INFO,
            )
            return openssl_root_dir

        possible_openssl_root_dirs = ["/opt/homebrew/opt/openssl@1.1", "/usr/local/opt/openssl@1.1"]

        for dir in possible_openssl_root_dirs:
            if os.path.isdir(dir):
                return dir

        return None

    def finalize_cmake_config_command_darwin(self, cmake_config_command: List[str]):
        openssl_root_dir = self.get_openssl_root_dir()
        if openssl_root_dir is not None:
            cmake_config_command.append(f"-DOPENSSL_ROOT_DIR={openssl_root_dir}")
        # otherwise trust CMake to find OpenSSL

    def finalize_cmake_config_command_win32(self, cmake_config_command: List[str]):
        cmake_config_command.append("-GMinGW Makefiles")

    def get_extra_link_args(self, libs: List[str]):
        # https://stackoverflow.com/a/45335363/6639989
        return [f"-l:lib{name}.a" for name in libs]

    def finalize_darwin(self, extension: Extension):
        libs = ["ssl", "crypto"]
        openssl_root_dir = self.get_openssl_root_dir()
        if self.static_openssl:
            extension.extra_link_args.extend([f"{openssl_root_dir}/lib/lib{lib}.a" for lib in libs])
        else:
            extension.libraries.extend(libs)

    def finalize_linux_like(self, extension: Extension, libs: List[str]):
        if self.static_openssl:
            extension.extra_link_args.extend(self.get_extra_link_args(libs))
        else:
            extension.libraries.extend(libs)

    def finalize_win32(self, extension: Extension):
        self.finalize_linux_like(extension, ["ssl", "crypto", "ws2_32"])

    def finalize_linux(self, extension: Extension):
        self.finalize_linux_like(extension, ["ssl", "crypto"])

    def get_cflags(self):
        return "{0} -Werror=all".format(os.getenv("CFLAGS", "")).strip()

    def build_mgclient_for(self, extension: Extension):
        """
        Builds mgclient library and configures the extension to be able to use
        the mgclient library as a static library.

        In this function all usage of mgclient refers to the client library
        and not the python extension module.
        """
        cmake_binary = self.get_cmake_binary()

        self.announce("Preparing the build environment for mgclient", level=log.INFO)

        extension_build_dir = Path(self.build_temp).absolute()
        mgclient_build_path = os.path.join(extension_build_dir, "mgclient_build")
        mgclient_install_path = os.path.join(extension_build_dir, "mgclient_install")

        self.announce(f"Using {mgclient_build_path} as build directory for mgclient", level=log.INFO)
        self.announce(f"Using {mgclient_install_path} as install directory for mgclient", level=log.INFO)

        os.makedirs(mgclient_build_path, exist_ok=True)
        mgclient_source_path = os.path.join(Path(__file__).absolute().parent, "mgclient")

        # CMake <3.13 versions don't support explicit build directory
        prev_working_dir = os.getcwd()
        os.chdir(mgclient_build_path)

        self.announce("Configuring mgclient", level=log.INFO)

        build_type = "Debug" if self.debug else "Release"
        install_libdir = "lib"
        install_includedir = "include"
        cmake_config_command = [
            cmake_binary,
            mgclient_source_path,
            f"-DCMAKE_INSTALL_LIBDIR={install_libdir}",
            f"-DCMAKE_INSTALL_INCLUDEDIR={install_includedir}",
            f"-DCMAKE_BUILD_TYPE={build_type}",
            f"-DCMAKE_INSTALL_PREFIX={mgclient_install_path}",
            "-DBUILD_TESTING=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            f'-DCMAKE_C_FLAGS="{self.get_cflags()}"',
            f"-DOPENSSL_USE_STATIC_LIBS={'ON' if self.static_openssl else 'OFF'}",
        ]

        finalize_cmake_config_command = getattr(self, "finalize_cmake_config_command_" + sys.platform, None)
        if finalize_cmake_config_command is not None:
            finalize_cmake_config_command(cmake_config_command)

        try:
            self.spawn(cmake_config_command)
        except DistutilsExecError as dee:
            self.announce("Error happened during configuring mgclient! Is OpenSSL installed correctly?")
            raise dee

        self.announce("Building mgclient binaries", level=log.INFO)

        try:
            self.spawn([cmake_binary, "--build", mgclient_build_path, "--config", build_type, "--target", "install"])
        except DistutilsExecError as dee:
            self.announce("Error happened during building mgclient binaries!", level=log.FATAL)
            raise dee

        os.chdir(prev_working_dir)

        mgclient_sources = [os.path.join(mgclient_source_path, "CMakeLists.txt")]

        for subdir in ["src", "include", "cmake"]:
            mgclient_sources.extend(list_all_files_in_dir(os.path.join(mgclient_source_path, subdir)))

        extension.include_dirs.append(os.path.join(mgclient_install_path, install_includedir))
        extension.extra_objects.append(os.path.join(mgclient_install_path, install_libdir, "libmgclient.a"))
        extension.depends.extend(mgclient_sources)
        extension.define_macros.append(("MGCLIENT_STATIC_DEFINE", ""))

        finalize = getattr(self, "finalize_" + sys.platform, None)
        if finalize is not None:
            finalize(extension)


setup(
    name="pymgclient",
    version="1.1.0",
    maintainer="Benjamin Antal",
    maintainer_email="benjamin.antal@memgraph.com",
    author="Marin Tomic",
    author_email="marin.tomic@memgraph.com",
    license="Apache2",
    platforms=["linux"],
    python_requires=">=3.6",
    description="Memgraph database adapter for Python language",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/memgraph/pymgclient",
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Programming Language :: Python :: 3.6",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: Implementation :: CPython",
        "Topic :: Database",
        "Topic :: Database :: Front-Ends",
        "Topic :: Software Development",
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Operating System :: POSIX :: Linux",
    ],
    ext_modules=[
        Extension(EXTENSION_NAME, sources=sources, depends=headers, extra_compile_args=["-Werror=all", "-std=c99"])
    ],
    project_urls={
        "Source": "https://github.com/memgraph/pymgclient",
        "Documentation": "https://memgraph.github.io/pymgclient",
    },
    cmdclass={"build_ext": BuildMgclientExt},
)
