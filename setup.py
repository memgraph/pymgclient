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

import sys
import os
import pathlib
import platform
import shutil

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from distutils import log
from distutils.core import DistutilsExecError, DistutilsPlatformError


IS_WINDOWS = sys.platform == 'win32'
IS_APPLE = sys.platform == 'darwin'
IS_X64 = platform.architecture()[0] == '64bit'

if IS_WINDOWS:
    # https://stackoverflow.com/a/57109148/6639989
    import distutils.cygwinccompiler
    distutils.cygwinccompiler.get_msvcr = lambda: []

with open('README.md', 'r') as fh:
    readme = fh.read()
    long_description = '\n'.join(readme.split('\n')[2:]).lstrip()

# Throughout this file "mgclient" can mean two different things:
# 1. The mgclient library which is the official Memgraph client library.
# 2. The mgclient python extension module which is a wrapper around the
#    client library.
EXTENSION_NAME = 'mgclient'

sources = [
    'column.c', 'connection-int.c', 'connection.c', 'cursor.c',
    'glue.c', 'mgclientmodule.c', 'types.c'
]
sources = [os.path.join('src', fn) for fn in sources]

headers = [
    'column.h', 'connection.h', 'cursor.h', 'exceptions.h', 'glue.h',
    'types.h'
]
headers = [os.path.join('src', fn) for fn in headers]


def list_all_files_in_dir(path):
    result = []
    for root, _dirs, files in os.walk(path):
        result.extend(os.path.join(root, f) for f in files)

    return result


class BuildMgclientExt(build_ext):
    '''
    Builds using cmake instead of the python setuptools implicit build
    '''

    def run(self):
        '''
        Perform build_cmake before doing the 'normal' stuff
        '''

        for extension in self.extensions:
            if extension.name == EXTENSION_NAME:
                self.build_mgclient_for(extension)

        if IS_WINDOWS:
            if self.compiler is None:
                self.compiler = 'mingw32'
            elif self.compiler != 'mingw32':
                raise DistutilsPlatformError(
                    f'The specified compiler {self.compiler} is not supported '
                    'on windows, only mingw32 is supported.')

        super().run()

    def get_cmake_binary(self):
        cmake_env_var_name = 'PYMGCLIENT_CMAKE'
        custom_cmake = os.getenv(cmake_env_var_name)
        if custom_cmake is None:
            # cmake3 is checked before cmake, because on CentOS cmake refers
            # to CMake 2.*
            for possible_cmake in ['cmake3', 'cmake']:
                self.announce(
                    f'Checking if {possible_cmake} can be used',
                    level=log.INFO)

                which_cmake = shutil.which(possible_cmake)
                if which_cmake is not None:
                    self.announce(
                        f'Using {which_cmake}', level=log.INFO)
                    return os.path.abspath(which_cmake)

                self.announce(
                    f'{possible_cmake} is not accesible', level=log.INFO)
            raise DistutilsExecError('Cannot found suitable cmake')
        else:
            self.announce(
                f'Using the value of {cmake_env_var_name} for CMake, which is'
                f'{custom_cmake}', level=log.INFO)
            return custom_cmake

    def get_cmake_generator(self):
        if IS_WINDOWS:
            return 'MinGW Makefiles'
        return None

    def get_extra_libraries(self):
        extra_libs = ['ssl']
        if IS_WINDOWS:
            extra_libs.extend(['crypto', 'ws2_32'])
        return extra_libs

    def get_extra_cmake_config_args(self):
        if not IS_APPLE:
            return []

        openssl_root_dir_env_var = 'OPENSSL_ROOT_DIR'
        openssl_root_dir = os.getenv(openssl_root_dir_env_var)

        if openssl_root_dir:
            self.announce(
                f'Using the value of {openssl_root_dir_env_var} for OpenSSL,'
                f'which is {openssl_root_dir}', level=log.INFO)
            return [f'-DOPENSSL_ROOT_DIR={openssl_root_dir}']

        default_openssl_root_dir = '/usr/local/Cellar/openssl@1.1'

        if not os.path.isdir(default_openssl_root_dir):
            # Maybe CMake can find OpenSSL properly without this
            return []

        self.announce('d', level=log.INFO)
        openssl_versions = sorted(
            pathlib.Path(default_openssl_root_dir).iterdir(),
            key=os.path.getmtime,
            reverse=True)

        if not openssl_versions:
            return []

        openssl_root_dir = str(openssl_versions[0])
        self.announce(
            f'Found OpenSSL in {openssl_root_dir}', level=log.INFO)
        return [f'-DOPENSSL_ROOT_DIR={openssl_root_dir}']

    def build_mgclient_for(self, extension: Extension):
        '''
        Builds mgclient library and configures the extension to be able to use
        the mgclient library as a static library.

        In this function all usage of mgclient refers to the client library
        and not the python extension module.
        '''
        cmake_binary = self.get_cmake_binary()

        self.announce(
            'Preparing the build environment for mgclient', level=log.INFO)

        extension_build_dir = pathlib.Path(self.build_temp).absolute()
        mgclient_build_path = os.path.join(
            extension_build_dir, 'mgclient_build')
        mgclient_install_path = os.path.join(
            extension_build_dir, 'mgclient_install')

        self.announce(
            f'Using {mgclient_build_path} as build directory for mgclient',
            level=log.INFO)
        self.announce(
            f'Using {mgclient_install_path} as install directory for mgclient',
            level=log.INFO)

        os.makedirs(mgclient_build_path, exist_ok=True)
        mgclient_source_path = os.path.join(pathlib.Path(
            __file__).absolute().parent, 'mgclient')

        # CMake <3.13 versions don't support explicit build directory
        prev_working_dir = os.getcwd()
        os.chdir(mgclient_build_path)

        self.announce('Configuring mgclient', level=log.INFO)

        build_type = 'Debug' if self.debug else 'Release'
        install_libdir = 'lib'
        install_includedir = 'include'
        cmake_config_command = [
            cmake_binary,
            mgclient_source_path,
            f'-DCMAKE_INSTALL_LIBDIR={install_libdir}',
            f'-DCMAKE_INSTALL_INCLUDEDIR={install_includedir}',
            f'-DCMAKE_BUILD_TYPE={build_type}',
            f'-DCMAKE_INSTALL_PREFIX={mgclient_install_path}',
            '-DBUILD_TESTING=OFF',
            '-DCMAKE_POSITION_INDEPENDENT_CODE=ON'
        ]
        cmake_config_command.extend(self.get_extra_cmake_config_args())
        generator = self.get_cmake_generator()

        if generator is not None:
            cmake_config_command.append(f'-G{generator}')

        try:
            self.spawn(cmake_config_command)
        except DistutilsExecError as dee:
            self.announce('Error happened during configuring mgclient! Is '
                          'OpenSSL installed correctly?')
            raise dee

        self.announce('Building mgclient binaries', level=log.INFO)

        try:
            self.spawn([cmake_binary,
                        '--build', mgclient_build_path,
                        '--config', build_type,
                        '--target', 'install'])
        except DistutilsExecError as dee:
            self.announce(
                'Error happened during building mgclient binaries!',
                level=log.FATAL)
            raise dee

        os.chdir(prev_working_dir)

        mgclient_sources = [os.path.join(
            mgclient_source_path, 'CMakeLists.txt')]

        for subdir in ['src', 'include', 'cmake']:
            mgclient_sources.extend(
                list_all_files_in_dir(
                    os.path.join(mgclient_source_path, subdir)))

        extension.include_dirs.append(os.path.join(
            mgclient_install_path, install_includedir))
        extension.extra_objects.append(os.path.join(
            mgclient_install_path, install_libdir, 'libmgclient.a'))
        extension.libraries.extend(self.get_extra_libraries())
        extension.depends.extend(mgclient_sources)
        extension.define_macros.append(('MGCLIENT_STATIC_DEFINE', ''))


setup(name='pymgclient',
      version='1.0.0',
      maintainer='Benjamin Antal',
      maintainer_email='benjamin.antal@memgraph.com',
      author="Marin Tomic",
      author_email="marin.tomic@memgraph.com",
      license='Apache2',
      platforms=['linux'],
      python_requires='>=3.5',
      description='Memgraph database adapter for Python language',
      long_description=long_description,
      long_description_content_type='text/markdown',
      url='https://github.com/memgraph/pymgclient',
      classifiers=[
          'Development Status :: 3 - Alpha',
          'Intended Audience :: Developers',
          'License :: OSI Approved :: Apache Software License',
          'Programming Language :: Python :: 3.5',
          'Programming Language :: Python :: 3.6',
          'Programming Language :: Python :: 3.7',
          'Programming Language :: Python :: 3.8',
          'Programming Language :: Python :: Implementation :: CPython',
          'Topic :: Database',
          'Topic :: Database :: Front-Ends',
          'Topic :: Software Development',
          'Topic :: Software Development :: Libraries :: Python Modules',
          'Operating System :: POSIX :: Linux'
      ],
      ext_modules=[
          Extension(EXTENSION_NAME,
                    sources=sources,
                    depends=headers)
      ],
      project_urls={
          'Source': 'https://github.com/memgraph/pymgclient',
          'Documentation': 'https://memgraph.github.io/pymgclient',
      },
      cmdclass={
          'build_ext': BuildMgclientExt
      })
