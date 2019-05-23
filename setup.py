# Copyright (c) 2016-2019 Memgraph Ltd. [https://memgraph.com]
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

from setuptools import setup, Extension

with open("README.md", "r") as fh:
    readme = fh.read()
    long_description = "\n".join(readme.split("\n")[2:]).lstrip()

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


setup(name="pymgclient",
      version="0.1.0",
      maintainer="Marin Tomic",
      maintainer_email="marin.tomic@memgraph.com",
      author="Marin Tomic",
      author_email="marin.tomic@memgraph.com",
      license="Apache2",
      platforms=["linux"],
      python_requires='>=3.5',
      description="Memgraph database adapter for Python language",
      long_description=long_description,
      long_description_content_type="text/markdown",
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
          Extension('mgclient',
                    sources=sources,
                    depends=headers,
                    libraries=['mgclient'])
      ],
      project_urls={
          'Source': 'https://github.com/memgraph/pymgclient',
          'Documentation': 'https://pymgclient.readthedocs.io/en/latest/'
      })
