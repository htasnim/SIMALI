#!/usr/bin/env bash

# Check if an argument is provided
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <debug|release|clean>"
    exit 1
fi

# Validate the argument
if [ "$1" != "debug" ] && [ "$1" != "release" ] && [ "$1" != "clean" ]; then
    echo "Invalid argument: '$1'"
    echo "Usage: $0 <debug|release|clean>"
    exit 1
fi

export UPCXX_THREADMODE=seq
export UPCXX_CODEMODE=opt
export UPCXX_NETWORK=ibv
module purge
module load boost/1.88.0-65dn
module load openmpi/4.1.7-e7k3
module load upcxx/2023.9.0-27k4
module load cmake/3.31.6-auvg
module load opencv/4.10.0-ki66

if [ -n "$SIMREEF_BUILD_ENV" ]; then
    source $SIMREEF_BUILD_ENV
fi

upcxx_exec=`which upcxx`

if [ -z "$upcxx_exec" ]; then
    echo "upcxx not found. Please load the appropriate module."
    exit 1
fi

upcxx_exec_canonical=$(readlink -f $upcxx_exec)
if [ "$upcxx_exec_canonical" != "$upcxx_exec" ]; then
    echo "Found symlink for upcxx - using target at $upcxx_exec_canonical"
    export PATH=`dirname $upcxx_exec_canonical`:$PATH
fi

set -e

rootdir=`pwd`

INSTALL_PATH=${SIMREEF_INSTALL_PATH:=$rootdir/install}

rm -rf $INSTALL_PATH/bin/simreef

BUILD_PATH=.build

if [ "$1" == "clean" ]; then
    echo Deleting $BUILD_PATH and $INSTALL_PATH
    rm -rf $BUILD_PATH
    rm -rf $INSTALL_PATH
    rm -rf src/.build
    exit 0
else
    mkdir -p $rootdir/$BUILD_PATH
    cd $rootdir/$BUILD_PATH
    if [ "$1" == "debug" ] || [ "$1" == "release" ]; then # Seems like this will always be true
	
        #rm -rf *
        #rm -rf $INSTALL_PATH/cmake
	SECONDS=0
    	cmake $rootdir -DCMAKE_BUILD_TYPE=$1 -DCMAKE_INSTALL_PREFIX=$INSTALL_PATH -DCMAKE_CXX_COMPILER=$(which mpicxx)
	echo "Build took $((SECONDS))s"
    fi
    echo "Installing to $INSTALL_PATH"
    make -j install
fi



