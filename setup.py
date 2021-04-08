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
import pathlib

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

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

        super().run()

    def build_mgclient_for(self, extension: Extension):
        '''
        Builds mgclient library and configures the extension to be able to use
        the mgclient library as a static library.

        In this function all usage of mgclient refers to the client library
        and not the python extension module.
        # '''

        self.announce('Preparing the build environment for mgclient', level=3)

        extension_build_dir = pathlib.Path(self.build_temp).absolute()

        mgclient_build_path = os.path.join(
            extension_build_dir, 'mgclient_build')
        mgclient_install_path = os.path.join(
            extension_build_dir, 'mgclient_install')

        self.announce(
            f'Using {mgclient_build_path} as build directory for mgclient',
            level=3)
        self.announce(
            f'Using {mgclient_install_path} as install directory for mgclient',
            level=3)

        os.makedirs(mgclient_build_path, exist_ok=True)
        mgclient_source_path = os.path.join(pathlib.Path(
            __file__).absolute().parent, 'mgclient')

        self.announce('Configuring mgclient', level=3)

        build_type = 'Debug' if self.debug else 'Release'
        self.spawn(['cmake',
                    '-S', mgclient_source_path,
                    '-B', mgclient_build_path,
                    f'-DCMAKE_BUILD_TYPE={build_type}',
                    f'-DCMAKE_INSTALL_PREFIX={mgclient_install_path}',
                    '-DBUILD_TESTING=OFF',
                    '-DCMAKE_POSITION_INDEPENDENT_CODE=ON'])

        self.announce('Building mgclient binaries', level=3)

        self.spawn(['cmake',
                    '--build', mgclient_build_path,
                   '--config', build_type,
                    '--target', 'install'])

        extension.include_dirs.append(os.path.join(
            mgclient_install_path, 'include'))
        extension.extra_objects.append(os.path.join(
            mgclient_install_path, 'lib', 'libmgclient.a'))
        extension.libraries.append('ssl')


setup(name='pymgclient',
      version='0.1.0',
      maintainer='Marin Tomic',
      maintainer_email='marin.tomic@memgraph.com',
      author='Marin Tomic',
      author_email='marin.tomic@memgraph.com',
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
