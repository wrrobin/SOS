#!/bin/sh

set -x

PAR_MAKE="-j 4"
SOS_GLOBAL_BUILD_OPTS="--enable-picky --enable-pmi-simple FCFLAGS=-fcray-pointer"
SOS_BUILD_OPTS="--disable-fortran --enable-error-checking --enable-lengthy-tests --with-oshrun-launcher=mpiexec.hydra"

# Set up the environment
mkdir $WORKSPACE/sos-install
export SOS_SRC=$WORKSPACE
export SOS_INSTALL=$WORKSPACE/sos-install

# Reading compiler dimension
if [ "$COMPILER" = "gcc" ]
then
    export CC=gcc
    which gcc
    gcc --version
    if [ ! -d "$JENKINS_HOME/deps/gcc-builds" ]
    then
        echo "Dependency build directory does not exist. Exiting."
    fi
    export DEP_BUILD_DIR=$JENKINS_HOME/deps/gcc-builds
elif [ "$COMPILER" = "icc" ]
then
    export CC=icc
    source /opt/intel/parallel_studio_xe_2017.4.056/psxevars.sh intel64
    which icc
    icc --version
    if [ ! -d "$JENKINS_HOME/deps/icc-builds" ]
    then
        echo "Dependency build directory does not exist. Exiting."
    fi
    export DEP_BUILD_DIR=$JENKINS_HOME/deps/icc-builds
else
    echo "Incompatible Compiler. Exiting."
    exit 1
fi

# Reading local transport dimension
if [ "$LOCAL_TRANSPORT" = "local-none" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS"
elif [ "$LOCAL_TRANSPORT" = "memcpy" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --enable-memcpy"
elif [ "$LOCAL_TRANSPORT" = "xpmem" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-xpmem"
elif [ "$LOCAL_TRANSPORT" = "cma" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-cma"
else
    echo "Invalid local transport. Exiting."
    exit 1
fi

# Reading remote transport dimension
if [ "$REMOTE_TRANSPORT" = "remote-none" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --without-portals4 --without-ofi"
elif [ "$REMOTE_TRANSPORT" = "portals4" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-portals4=$DEP_BUILD_DIR/portals4/"
elif [ "$REMOTE_TRANSPORT" = "ofi" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-ofi=$DEP_BUILD_DIR/libfabric/"
elif [ "$REMOTE_TRANSPORT" = "ofi-completion-polling" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-ofi=$DEP_BUILD_DIR/libfabric/ --with-completion-polling"
else
    echo "Invalid remote transport. Exiting."
    exit 1
fi

# Reading mr mode dimension
if [ "$MR_MODE" = "mr-basic" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --disable-mr-scalable"
elif [ "$MR_MODE" = "mr-scalable" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS"
elif [ "$MR_MODE" = "mr-fxr" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --enable-mr-rma-event"
elif [ "$MR_MODE" = "mr-remote-va" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --enable-remote-virtual-addressing"
else
    echo "Invalid MR mode. Exiting."
    exit 1
fi

# Reading collective algorithm dimension
if [ "$COLLECTIVE_ALGORITHM" = "auto" ]
then
    export SHMEM_BARRIER_ALGORITHM=auto 
    export SHMEM_BCAST_ALGORITHM=auto 
    export SHMEM_REDUCE_ALGORITHM=auto 
    export SHMEM_COLLECT_ALGORITHM=auto 
    export SHMEM_FCOLLECT_ALGORITHM=auto
elif [ "$COLLECTIVE_ALGORITHM" = "linear" ]
then
    export SHMEM_BARRIER_ALGORITHM=linear
    export SHMEM_BCAST_ALGORITHM=linear
    export SHMEM_REDUCE_ALGORITHM=linear
    export SHMEM_COLLECT_ALGORITHM=linear
    export SHMEM_FCOLLECT_ALGORITHM=linear
elif [ "$COLLECTIVE_ALGORITHM" = "tree" ]
then
    export SHMEM_BARRIER_ALGORITHM=tree
    export SHMEM_BCAST_ALGORITHM=tree
    export SHMEM_REDUCE_ALGORITHM=tree
elif [ "$COLLECTIVE_ALGORITHM" = "dissem-recdbl" ]
then
    export SHMEM_BARRIER_ALGORITHM=dissem
    export SHMEM_REDUCE_ALGORITHM=recdbl
    export SHMEM_FCOLLECT_ALGORITHM=recdbl
elif [ "$COLLECTIVE_ALGORITHM" = "ring" ]
then
    export SHMEM_FCOLLECT_ALGORITHM=ring
else
    echo "Invalid Algorithm option. Exiting."
    exit 1
fi

# Reading threads dimension
if [ $THREADS = "enabled" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS"
elif [ $THREADS = "enabled-completion" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --enable-thread-completion"
elif [ $THREADS = "disabled" ]
then
    SOS_BUILD_OPTS="$SOS_BUILD_OPTS --disable-threads"
else
    echo "Invalid Threads option. Exiting."
    exit 1
fi


# Build SOS
cd $SOS_SRC
./autogen.sh
export BASE_PATH=$PATH
export PATH=$DEP_BUILD_DIR/hydra/bin:$DEP_BUILD_DIR/portals4/bin:$BASE_PATH
./configure --prefix=$SOS_INSTALL $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
make $PAR_MAKE
make $PAR_MAKE check TESTS=
make install
mpiexec -np 1 test/unit/hello

echo "Test complete."
