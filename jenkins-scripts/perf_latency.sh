#!/bin/sh

PAR_MAKE="-j 4"
SOS_GLOBAL_BUILD_OPTS="--enable-picky --enable-pmi-simple"
SOS_BUILD_OPTS="--disable-fortran --enable-threads --enable-thread-completion --enable-completion-polling --enable-error-checking --enable-lengthy-tests --with-oshrun-launcher=mpiexec.hydra"
RESULT_DIR=$JENKINS_HOME/results

# Set up the environment
mkdir $WORKSPACE/sos-install
export SOS_SRC=$WORKSPACE
export SOS_INSTALL=$WORKSPACE/sos-install
if [ ! -d "$RESULT_DIR" ]
then
    mkdir $RESULT_DIR
fi

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
    source $WORKSPACE/jenkins-scripts/setup_intel.sh
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
SOS_BUILD_OPTS="$SOS_BUILD_OPTS --with-ofi=/home/rahmanmd/libfabric-git/install"

# Build SOS
cd $SOS_SRC
./autogen.sh
export BASE_PATH=$PATH
export PATH=$DEP_BUILD_DIR/hydra/bin:$BASE_PATH
cd $SOS_SRC
./configure --prefix=$SOS_INSTALL $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
make $PAR_MAKE
make $PAR_MAKE check TESTS=
make install

export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
export OSHRUN_LAUNCHER="mpiexec.hydra"

export BENCH_HOME=$SOS_SRC/test/performance/shmem_perf_suite
cat > job.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
export RESULT_DIR=$JENKINS_HOME/results
export COMPILER
export BENCHMARK
scontrol show hostnames > hostfile

if [ -f "$RESULT_DIR/lat_$BENCHMARK_$COMPILER" ]
then
    rm -rf $RESULT_DIR/lat_"$BENCHMARK"_"$COMPILER"
fi

oshrun -np 2 -ppn 1 -f hostfile $BENCH_HOME/$BENCHMARK > out_$BENCHMARK
cat out_$BENCHMARK | grep "in bytes" -A24 | tail -n 22 | head -n 10 | awk '{print $2}' > tmp
sed '$!{:a;N;s/\n/,/;ta}' tmp > $WORKSPACE/lat_"$BENCHMARK"_"$COMPILER"
sed -i '1s/^/4B,8B,16B,32B,64B,128B,256B,512B,1024B,2048B\n/' $WORKSPACE/lat_"$BENCHMARK"_"$COMPILER"
cp out_$BENCHMARK $RESULT_DIR/lat_"$BENCHMARK"_"$COMPILER"
rm tmp

EOF

chmod +x job.sh
salloc -J "SOS_$BENCHMARK" -N 2 -t 30 ./job.sh

