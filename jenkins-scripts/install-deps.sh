#!/bin/sh

set -x

PAR_MAKE="-j 4"

if [ ! -d "$JENKINS_HOME" ]
then
    echo "JENKINS_HOME is not set properly. Exiting."
    exit 1
fi

if [ -d "$JENKINS_HOME/deps" ]
then
    rm -rf $JENKINS_HOME/deps
fi

mkdir $JENKINS_HOME/deps
mkdir $JENKINS_HOME/deps/downloads
mkdir $JENKINS_HOME/deps/gcc-builds
mkdir $JENKINS_HOME/deps/icc-builds

DOWNLOADS=$JENKINS_HOME/deps/downloads
GCC_BUILDS=$JENKINS_HOME/deps/gcc-builds
ICC_BUILDS=$JENKINS_HOME/deps/icc-builds

# Start GCC builds
export CC=gcc
which gcc
gcc --version

## Build libev
cd $DOWNLOADS
wget http://dist.schmorp.de/libev/Attic/libev-4.22.tar.gz
tar -xzvf libev-4.22.tar.gz
cd libev-4.22
./configure --prefix=$GCC_BUILDS/libev
make
make install

## Build Portals 4
cd $DOWNLOADS
git clone --depth 10 https://github.com/regrant/portals4.git portals4
cd portals4
./autogen.sh
./configure --prefix=$GCC_BUILDS/portals4/ --with-ev=$GCC_BUILDS/libev --enable-zero-mrs --enable-reliable-udp --disable-pmi-from-portals
# JSD: --enable-transport-shmem removed; it was causing tests to hang
make $PAR_MAKE
make install

## Build libfabric
cd $DOWNLOADS
git clone -b v1.5.x --depth 10 https://github.com/ofiwg/libfabric.git libfabric
cd libfabric
./autogen.sh
./configure --prefix=$GCC_BUILDS/libfabric
make $PAR_MAKE
make install

## Build Hydra
cd $DOWNLOADS
wget http://www.mpich.org/static/downloads/3.2/hydra-3.2.tar.gz
tar xvzf hydra-3.2.tar.gz
cd hydra-3.2/
./configure --prefix=$GCC_BUILDS/hydra
make $PAR_MAKE
make install

## Fetch UH Tests
cd $DOWNLOADS
git clone --depth 10 https://github.com/openshmem-org/tests-uh.git tests-uh

## Fetch Cray Tests
cd $DOWNLOADS
git clone --depth 10 https://github.com/openshmem-org/tests-cray.git tests-cray

## Fetch Mellanox Tests
cd $DOWNLOADS
git clone --depth 10 https://github.com/openshmem-org/tests-mellanox.git tests-mellanox

## Fetch ISx
cd $DOWNLOADS
git clone --depth 10 https://github.com/ParRes/ISx.git ISx

## Fetch PRK
cd $DOWNLOADS
git clone --depth 10 https://github.com/ParRes/Kernels.git PRK
echo -e "SHMEMCC=oshcc -std=c99\nSHMEMTOP=$$JENKINS_INSTALL\n" > PRK/common/make.defs

## Build Libevent
cd $DOWNLOADS
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar -xzvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./autogen.sh
./configure --prefix=$GCC_BUILDS/libevent
make clean all install

## Build PMIx
cd $DOWNLOADS
git clone --depth 10 --single-branch -b v2.0 https://github.com/pmix/pmix pmix
cd pmix
./autogen.pl
./configure --prefix=$GCC_BUILDS/pmix --with-libevent=$GCC_BUILDS/libevent --with-devel-headers --disable-visibility
make clean all install

## Build OMPI from source (takes too long, so removed in favor of RPM install below)
#cd $DOWNLOADS
#git clone --depth 10 --single-branch -b v3.0.x https://github.com/open-mpi/ompi.git ompi
#cd ompi
#./autogen.pl
#./configure --prefix=$GCC_BUILDS/ompi --with-libevent=$GCC_BUILDS/libevent --with-pmix=$GCC_BUILDS/pmix
#make clean all install

## Download and install OpenMPI RPM 
#cd $GCC_BUILDS
#wget http://gdurl.com/P5og -O openmpi-v3.0.x.deb
#sudo dpkg -i openmpi-v3.0.x.deb


# Start ICC builds
source /opt/intel/parallel_studio_xe_2017.4.056/psxevars.sh intel64
export CC=icc
which icc
icc --version

## Build libev
cd $DOWNLOADS
if [ -d libev-4.22 ]
then
    rm -rf libev-4.22
fi
tar -xzvf libev-4.22.tar.gz
cd libev-4.22
./configure --prefix=$ICC_BUILDS/libev
make
make install

## Build Portals 4
cd $DOWNLOADS
if [ -d portals ]
then
    rm -rf portals4
fi
git clone --depth 10 https://github.com/regrant/portals4.git portals4
cd portals4
./autogen.sh
./configure --prefix=$ICC_BUILDS/portals4/ --with-ev=$ICC_BUILDS/libev --enable-zero-mrs --enable-reliable-udp --disable-pmi-from-portals
# JSD: --enable-transport-shmem removed; it was causing tests to hang
make $PAR_MAKE
make install

## Build libfabric
cd $DOWNLOADS
if [ -d libfabric ]
then
    rm -rf libfabric
fi
git clone -b v1.5.x --depth 10 https://github.com/ofiwg/libfabric.git libfabric
cd libfabric
./autogen.sh
./configure --prefix=$ICC_BUILDS/libfabric
make $PAR_MAKE
make install

## Build Hydra
cd $DOWNLOADS
if [ -d hydra-3.2 ]
then
    rm -rf hydra-3.2
fi
wget http://www.mpich.org/static/downloads/3.2/hydra-3.2.tar.gz
tar xvzf hydra-3.2.tar.gz
cd hydra-3.2/
./configure --prefix=$ICC_BUILDS/hydra
make $PAR_MAKE
make install

## Build Libevent
cd $DOWNLOADS
if [ -d libevent-2.0.22-stable ]
then
    rm -rf libevent-2.0.22-stable
fi
tar -xzvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./autogen.sh
./configure --prefix=$ICC_BUILDS/libevent
make clean all install

## Build PMIx
cd $DOWNLOADS
if [ -d pmix ]
then
    rm -rf pmix
fi
git clone --depth 10 --single-branch -b v2.0 https://github.com/pmix/pmix pmix
cd pmix
./autogen.pl
./configure --prefix=$ICC_BUILDS/pmix --with-libevent=$ICC_BUILDS/libevent --with-devel-headers --disable-visibility
make clean all install

echo "All build complete."
