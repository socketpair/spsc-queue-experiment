# Order of import is SIGNIFICANT (!)
# https://stackoverflow.com/questions/21594925/error-each-element-of-ext-modules-option-must-be-an-extension-instance-or-2-t
from Cython.Build import cythonize
from setuptools import Extension, find_packages, setup

setup(
    name='ringstat',
    version='12.0',
    description='Ideco ringstat',
    packages=find_packages(),
    # https://stackoverflow.com/questions/26833947/how-can-i-set-cython-compiler-flags-when-using-pyximport/26834595
    ext_modules=cythonize([Extension(
        name="ringstat",
        sources=["ringstat.pyx"],
        libraries=["rt"],
        extra_compile_args=['-Wall', '-Wextra', '-Werror', '-Wno-error=unused-function'],
    )]),
    zip_safe=False,
    package_data={pkg: ['py.typed'] for pkg in find_packages(exclude=['tests', 'tests.*'])},
)
