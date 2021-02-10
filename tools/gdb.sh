#!/bin/sh
set -eu

# Check that gdb is installed.
if ! command -v gdb >/dev/null; then
	cat <<NOGDB
gdb is not installed or not found in the PATH.

Install gdb or adjust you PATH if you are using non-system gdb and
try once more.
NOGDB
	exit 1;
fi

VERSION=${PWD}/version

# Check the location: if the coredump artefacts are collected via
# `tarabrt.sh' there should be /version file in the root of the
# unpacked tarball. Otherwise, there is no guarantee the coredump
# is collected the right way and we can't proceed loading it.
if [ ! -f "${VERSION}" ]; then
	cat <<NOARTEFACTS
${VERSION} file is missing.

If the coredump artefacts are collected via \`tararbrt.sh' tool
there should be /version file in the root of the unpacked tarball
(i.e. ${PWD}).
If version file is missing, there is no guarantee the coredump
is collected the right way and its loading can't be proceeded
with this script. Check whether current working directory is the
tarball root, or try load the core dump file manually.
NOARTEFACTS
	exit 1;
fi

REVISION=$(grep -oP 'Tarantool \d+\.\d+\.\d+-\d+-g\K[a-f0-9]+' "$VERSION")
cat <<SOURCES
================================================================================

Do not forget to properly setup the environment:
* git clone https://github.com/tarantool/tarantool.git sources
* cd !$
* git checkout $REVISION
* git submodule update --recursive --init

================================================================================
SOURCES

# Define the build path to be substituted with the source path.
# XXX: Check the absolute path on the function <main> definition
# considering it is located in src/main.cc within Tarantool repo.
SUBPATH=$(gdb -batch -n ./tarantool -ex 'info line main' | \
	grep -oP 'Line \d+ of \"\K.+(?=\/src\/main\.cc\")')

# Launch gdb and load coredump with all related artefacts.
gdb ./tarantool \
    -ex "set sysroot $(realpath .)" \
    -ex "set substitute-path $SUBPATH sources" \
    -ex 'core coredump'
