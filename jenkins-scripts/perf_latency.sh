#!/bin/sh

COMPILER=${1}
PAR_MAKE="-j 4"
SOS_GLOBAL_BUILD_OPTS="--enable-picky --enable-pmi-simple FCFLAGS=-fcray-pointer"
SOS_BUILD_OPTS="--disable-fortran --enable-threads --enable-thread-completion --enable-remote-virtual-addressing --enable-completion-polling --enable-error-checking --enable-lengthy-tests --with-oshrun-launcher=mpiexec.hydra"

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
        exit 1
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
        exit 1
    fi
    export DEP_BUILD_DIR=$JENKINS_HOME/deps/icc-builds
else
    echo "Incompatible Compiler. Exiting."
    exit 1
fi

# With OFI installation
SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-ofi=$DEP_BUILD_DIR/libfabric/"

# Build SOS
cd $SOS_SRC
./autogen.sh
export BASE_PATH=$PATH
export PATH=$DEP_BUILD_DIR/hydra/bin:$DEP_BUILD_DIR/portals4/bin:$BASE_PATH
cd $SOS_SRC
./configure --prefix=$SOS_INSTALL $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
make $PAR_MAKE
make install
make $PAR_MAKE check TESTS=

export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
export OSHRUN_LAUNCHER="mpiexec.hydra"

export BENCH_HOME=$SOS_SRC/test/performance/shmem_perf_suite
cat > job.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
scontrol show hostnames > hostfile

oshrun -np 2 -ppn 1 -f hostfile $BENCH_HOME/$BENCHMARK

EOF

chmod +x job.sh
salloc -J "SOS_$BENCHMARK" -N 2 -t 30 ./job.sh

