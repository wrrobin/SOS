#!/bin/sh

set -x

PAR_MAKE="-j 4"
SOS_GLOBAL_BUILD_OPTS="--enable-picky --enable-pmi-simple FCFLAGS=-fcray-pointer"
SOS_BUILD_OPTS="--disable-fortran --enable-threads --enable-thread-completion --enable-remote-virtual-addressing --enable-completion-polling --enable-error-checking --enable-lengthy-tests --with-oshrun-launcher=mpiexec.hydra"

# Set up the environment
if [ -d $WORKSPACE/jenkins ] 
then
	rm -rf $WORKSPACE/jenkins
fi
mkdir $WORKSPACE/jenkins
mkdir $WORKSPACE/jenkins/src
mkdir $WORKSPACE/jenkins/install
export SOS_SRC=$WORKSPACE
export JENKINS_SRC=$WORKSPACE/jenkins/src
export JENKINS_INSTALL=$WORKSPACE/jenkins/install

# Set up Compiler
if [ "$COMPILER" = "gcc" ]
then
    export CC=gcc
    which gcc
    gcc --version
elif [ "$COMPILER" = "icc" ]
then
    export CC=icc
    source /opt/intel/parallel_studio_xe_2017.4.056/psxevars.sh intel64
    which icc
    icc --version
else
    echo "Incompatible Compiler. Exiting."
    exit 1
fi

if [ "COLLECTIVE_ALGORITHM" = "auto" ]
then
    export SHMEM_BARRIER_ALGORITHM=auto 
    export SHMEM_BCAST_ALGORITHM=auto 
    export SHMEM_REDUCE_ALGORITHM=auto 
    export SHMEM_COLLECT_ALGORITHM=auto 
    export SHMEM_FCOLLECT_ALGORITHM=auto
elif [ "COLLECTIVE_ALGORITHM" = "linear" ]
then
    export SHMEM_BARRIER_ALGORITHM=linear
    export SHMEM_BCAST_ALGORITHM=linear
    export SHMEM_REDUCE_ALGORITHM=linear
    export SHMEM_COLLECT_ALGORITHM=linear
    export SHMEM_FCOLLECT_ALGORITHM=linear
elif [ "COLLECTIVE_ALGORITHM" = "tree" ]
then
    export SHMEM_BARRIER_ALGORITHM=tree
    export SHMEM_BCAST_ALGORITHM=tree
    export SHMEM_REDUCE_ALGORITHM=tree
elif [ "COLLECTIVE_ALGORITHM" = "dissem_recdbl" ]
then
    export SHMEM_BARRIER_ALGORITHM=dissem
    export SHMEM_REDUCE_ALGORITHM=recdbl
    export SHMEM_FCOLLECT_ALGORITHM=recdbl
elif [ "COLLECTIVE_ALGORITHM" = "ring" ]
    export SHMEM_FCOLLECT_ALGORITHM=ring
then
else
    echo "Invalid Algorithm option. Exiting."
    exit 1
fi

# Build libev
cd $JENKINS_SRC
wget http://dist.schmorp.de/libev/Attic/libev-4.22.tar.gz
tar -xzvf libev-4.22.tar.gz
cd libev-4.22 
./configure --prefix=$JENKINS_INSTALL/libev 
make 
make install

# Build Portals 4
cd $JENKINS_SRC
git clone --depth 10 https://github.com/regrant/portals4.git portals4
cd portals4
./autogen.sh
./configure --prefix=$JENKINS_INSTALL/portals4/ --with-ev=$JENKINS_INSTALL/libev --enable-zero-mrs --enable-reliable-udp --disable-pmi-from-portals
# JSD: --enable-transport-shmem removed; it was causing tests to hang
make $PAR_MAKE
make install

# Build libfabric
cd $JENKINS_SRC
git clone -b v1.5.x --depth 10 https://github.com/ofiwg/libfabric.git libfabric
cd libfabric
./autogen.sh
./configure --prefix=$JENKINS_INSTALL/libfabric
make $PAR_MAKE
make install

# Build Hydra
cd $JENKINS_SRC
wget http://www.mpich.org/static/downloads/3.2/hydra-3.2.tar.gz
tar xvzf hydra-3.2.tar.gz
cd hydra-3.2/
./configure --prefix=$JENKINS_INSTALL/hydra
make $PAR_MAKE
make install

# Build SOS
cd $SOS_SRC
./autogen.sh
export BASE_PATH=$PATH

if [ "$TRANSPORT" = "transport-none" ]
then
	cd $SOS_SRC
	mkdir no-transport-build
	cd no-transport-build
	export PATH=$JENKINS_INSTALL/hydra/bin:$BASE_PATH
	../configure --prefix=$JENKINS_INSTALL/sandia-shmem-none --without-ofi --without-portals4 $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
	make $PAR_MAKE
	make $PAR_MAKE check TESTS=
	make install
	mpiexec -np 1 test/unit/hello
elif [ "$TRANSPORT" = "transport-cma" ]
then
	cd $SOS_SRC
	mkdir cma-build
	cd cma-build
	export PATH=$JENKINS_INSTALL/hydra/bin:$BASE_PATH
	../configure --prefix=$JENKINS_INSTALL/sandia-shmem-cma --with-cma $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
	make $PAR_MAKE
	make $PAR_MAKE check TESTS=
	make install
	mpiexec -np 1 test/unit/hello
elif [ "$TRANSPORT" = "transport-portals4" ]
then
	cd $SOS_SRC
	mkdir portals4-build
	cd portals4-build
	export PATH=$JENKINS_INSTALL/hydra/bin:$JENKINS_INSTALL/portals4/bin:$BASE_PATH
	../configure --with-portals4=$JENKINS_INSTALL/portals4/ --prefix=$JENKINS_INSTALL/sandia-shmem-portals4 $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
	make $PAR_MAKE
	make $PAR_MAKE check TESTS=
	#- make VERBOSE=1 TEST_RUNNER="mpiexec.hydra -np 2 timeout 10" check
	make install
	mpiexec -np 1 test/unit/hello
elif [ "$TRANSPORT" = "transport-ofi" ]
then
	cd $SOS_SRC
	mkdir ofi-build
	cd ofi-build
	export PATH=$JENKINS_INSTALL/hydra/bin:$BASE_PATH
	../configure --with-ofi=$JENKINS_INSTALL/libfabric/ --prefix=$JENKINS_INSTALL/sandia-shmem-ofi $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
	make $PAR_MAKE
	make $PAR_MAKE check TESTS=
	make VERBOSE=1 TEST_RUNNER="mpiexec.hydra -np 2" check
	make install
else
	echo "Invalid transport protocol. Exiting."
	exit 1
fi

