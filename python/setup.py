from distutils.core import setup, Extension
from distutils.command.build_ext import build_ext
import glob
import os
import sys

SRC_DIR = os.environ.get('HEELHOOK_SRC', '../src')

# This is probably totally the wrong way to do this, but I didn't want to
# duplicate all the compiler options for heelhook in this file. So, just
# shell out and build heelhook
class hhBuildExt(build_ext):
    def build_extension(self, ext):
        cwd = os.getcwd()
        os.chdir(SRC_DIR)

        if self.debug:
            cmd = 'make debug heelhook_static'
        else:
            cmd = 'make heelhook_static'

        print
        print 'building heelhook'
        if os.system(cmd) != 0:
            sys.stderr.write('heelhook build failed, aborting\n');
            sys.exit(1)
        os.chdir(cwd)
        print

        return build_ext.build_extension(self, ext)

# build heelhook objects to link with
base_names = [
    'hhmemory', 'darray', 'protocol', 'sha1', 'cencode', 'util',
    'error_code', 'endpoint', 'hhlog', 'event', 'server', 'pqueue'
]

library_path = os.path.join(SRC_DIR, 'libheelhook.a')
deps = [library_path] + glob.glob('*.c') + glob.glob('*.h')
objs = [library_path]

setup(
    name='heelhook',
    version='1.0',
    py_modules=['heelhook'],
    ext_modules=[
        Extension(
            name='_heelhook',
            sources=['_heelhook.c'],
            include_dirs=[SRC_DIR],
            extra_objects=objs,
            extra_compile_args=["-std=c99"],
            depends=deps
        )
    ],
    cmdclass=dict(build_ext=hhBuildExt)
)
