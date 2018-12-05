# Python API for the nGraph Library

## Build the nGraph Python API

First check [build instructions] for platforms that nGraph supports and
prerequisites.

On top of that. On Linux, you would need to make sure that `patchelf` is installed.
If missing,

On Ubuntu or .deb based system run

    apt install patchelf

On Centos or .rpm based system run

    yum install patchelf

You can build nGraph Python API without first building nGraph separately.
From the [build instructions] to build nGraph, the only thing you need to change is
adding an additional option `-DNGRAPH_PYTHON_BUILD_ENABLE=TRUE` when you call `cmake`

All the examples below assume you are creating a build directory `build` parallel to
nGraph source.

    $ cmake <OTHER_OPTIONS> -DNGRAPH_PYTHON_BUILD_ENABLE=TRUE <PATH_TO_NGRAPH_SRC_ROOT>

For example:

    $ cmake -DNGRAPH_DEX_ONLY=TRUE -DNGRAPH_PYTHON_BUILD_ENABLE=TRUE ../ngraph

Build the Python API wheel:

    $ make -j<NUM_THREADS_ON_YOUR_CPU> python_wheel

For example:

    $ make -j8 python wheel

Then wheel file will be created under `dist`

For example:

    dist/ngraph_core-0.10.0-cp27-cp27mu-linux_x86_64.whl

## Running tests with tox

[Tox] is a Python [virtualenv] management and test command line tool. In our
project it automates:

* running of unit tests with [pytest]
* checking that code style is compliant with [PEP8] using [Flake8]
* static type checking using [MyPy]
* testing across Python 2 and 3


Installing and running test with Tox in the `build` directory:

    $ pip install tox
    $ tox


You can run tests using only Python 3 or 2 using the `-e` (environment) switch:

    $ tox -e py36
    $ tox -e py27


You can check styles in a particular code directory by specifying the path:

    $ tox ngraph/


If you run into any problems, try recreating the virtual environments by
deleting the `.tox` directory:

    $ rm -rf .tox
    $ tox

[build instructions]:http://ngraph.nervanasys.com/docs/latest/buildlb.html
[Tox]:https://tox.readthedocs.io/
[virtualenv]:https://virtualenv.pypa.io/
[pytest]:https://docs.pytest.org/
[PEP8]:https://www.python.org/dev/peps/pep-0008
[Flake8]:http://flake8.pycqa.org
[MyPy]:http://mypy.readthedocs.io
