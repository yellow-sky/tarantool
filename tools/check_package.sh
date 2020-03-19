#!/bin/bash
set -ue

# script start up options parser
ver="${1-}"
imgs="${2-}"
rels="${3-}"

# get current git version of the Tarantool, if not specified in options
if [ "$ver" == "" ]; then
    # some OS like CentOS/Fedora use only 6 hexadecimal
    # digits as the abbreviated object name
    ver=$(git describe --abbrev=6 | sed 's#-#.#g')
fi

# set images names, if not specified in options
if [ "$imgs" == "" ]; then
    for dist in 14.04 16.04 18.04 19.10 ; do imgs="$imgs ubuntu:$dist" ; done
    for dist in jessie stretch buster ; do imgs="$imgs debian:$dist" ; done
    for dist in 6 7 8 ; do imgs="$imgs centos:$dist" ; done
    for dist in 28 29 30 31 ; do imgs="$imgs fedora:$dist" ; done
fi
# workaround: change 'el' image naming to 'centos'
imgs=$(echo $imgs | sed "s#el:#centos:#g")

# set release repository to be used for packages installation from,
# if not specified in options
if [ "$rels" == "" ]; then
    rels="1.10 2.1 2.2 2.3 2.4"
fi

# get current working directory of the running script
cwd=`dirname $0`

# Helper to run docker builder per different arguments:
# $1 - tarantool version expected to get on success
# $2 - image name based on OS naming
# $3 - release version that will be asked to install from repo
run_docker () {
    VER=$1
    IMG=$2
    REL=$3

    PKG_TOOL=yum
    if [[ $IMG =~ .*ubuntu.* || $IMG =~ .*debian.* ]]; then
        PKG_TOOL=apt
    fi
    echo "Testing $REL Tarantool on $IMG image ..."
    newimg="check-s3_"`echo $IMG | sed 's#:#-#g'`"_$REL"
    # repository update command must always be rerun to be able
    # to get the new available versions of the Tarantool, so the
    # flag "--no-cache" must be set
    docker build \
        --network=host \
        --no-cache \
        --build-arg PKG_TOOL=$PKG_TOOL \
        --build-arg REL=$REL \
        --build-arg IMG=$IMG \
        --build-arg TNT_VER=$VER \
        -f $cwd/Dockerfile.s3 -t $newimg .
}

# main routine
for rel in $rels ; do
    for img in $imgs ; do
        run_docker $ver $img $rel
    done
done
