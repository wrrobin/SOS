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
    source $WORKSPACE/jenkins-scripts/setup_gnu.sh
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

# With OFI installation (Need to install and use, will be done later, pre-installed for now)
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

if [ -f "$RESULT_DIR/bw-$BENCHMARK-$COMPILER" ]
then
    rm -rf $RESULT_DIR/bw-"$BENCHMARK"-"$COMPILER"
    rm -rf $RESULT_DIR/mr-"$BENCHMARK"-"$COMPILER"
fi

oshrun -np 2 -ppn 1 -f hostfile $BENCH_HOME/$BENCHMARK > out_$BENCHMARK
cat out_$BENCHMARK | grep "in bytes" -A24 | tail -n 18 | head -n 10 | awk '{print $2}' > tmp
cat out_$BENCHMARK | grep "in bytes" -A24 | tail -n 18 | head -n 10 | awk '{print $3}' > tmp2
sed '$!{:a;N;s/\n/,/;ta}' tmp > $WORKSPACE/bw_"$BENCHMARK"_"$COMPILER"
sed '$!{:a;N;s/\n/,/;ta}' tmp2 > $WORKSPACE/mr_"$BENCHMARK"_"$COMPILER"

sed -i '1s/^/64B,128B,256B,512B,1KB,2KB,4KB,8KB,16KB,32KB\n/' $WORKSPACE/bw_"$BENCHMARK"_"$COMPILER"
sed -i '1s/^/64B,128B,256B,512B,1KB,2KB,4KB,8KB,16KB,32KB\n/' $WORKSPACE/mr_"$BENCHMARK"_"$COMPILER"

cat out_$BENCHMARK | grep "in bytes" -A24 | tail -n 24 | awk '{print $1"\t"$2}' > $RESULT_DIR/bw-"$BENCHMARK"-"$COMPILER"
cat out_$BENCHMARK | grep "in bytes" -A24 | tail -n 24 | awk '{print $1"\t"$3}' > $RESULT_DIR/mr-"$BENCHMARK"-"$COMPILER"

rm tmp
rm tmp2

EOF

chmod +x job.sh
salloc -J "SOS_$BENCHMARK" -N 2 -t 30 ./job.sh

