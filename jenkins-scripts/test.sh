#!/bin/sh

set -x

COMPILER=${1}
PAR_MAKE="-j 4"
# Set up the environment
export SOS_SRC=$WORKSPACE
export SOS_INSTALL=$WORKSPACE/sos-install
SOS_DISABLE_FORTRAN=0

if [ -z "$(ls -A $SOS_INSTALL)" ] 
then
    echo "SOS install dir is empty. Exiting."
    exit 1
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

# Check whether fortran is disabled
if [ "$BUILD_FLAGS" = "fortran" ]
then
    SOS_DISABLE_FORTRAN=1
else
    SOS_DISABLE_FORTRAN=0
fi

export BASE_PATH=$PATH
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
export OSHRUN_LAUNCHER="mpiexec.hydra"
export SOS_DISABLE_FORTRAN

# ISx
BENCH_HOME=$JENKINS_HOME/deps/downloads/ISx/SHMEM
cd $BENCH_HOME
make CC=oshcc LDLIBS=-lm
# Note: This SHMEM_SYMMETRIC_SIZE setting may exceed the memory available in the CI testing environment
cat > sos-isx.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH 
export BENCH_HOME=$JENKINS_HOME/deps/downloads/ISx/SHMEM

oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.strong 134217728 output_strong
oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.weak 33554432 output_weak
oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.weak_iso 33554432 output_weak_iso
make clean

EOF

chmod +x sos-isx.sh
salloc -J "ISx-SOS" -N 1 -t 30 ./sos-isx.sh

# PRK
BENCH_HOME=$JENKINS_HOME/deps/downloads/PRK
cd $BENCH_HOME
make allshmem
cat > sos-prk.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH
export BENCH_HOME=$JENKINS_HOME/deps/downloads/PRK

oshrun -np 4 $BENCH_HOME/SHMEM/Stencil/stencil 100 1000
oshrun -np 4 $BENCH_HOME/SHMEM/Synch_p2p/p2p 10 1000 1000
oshrun -np 4 $BENCH_HOME/SHMEM/Transpose/transpose 10 1000

make clean

EOF

chmod +x sos-prk.sh
salloc -J "PRK-SOS" -N 1 -t 30 ./sos-prk.sh

# Mellanox
BENCH_HOME=$JENKINS_HOME/deps/downloads/tests-mellanox
cd $BENCH_HOME/verifier
./autogen.sh
./configure --prefix=$BENCH_HOME/install CFLAGS=" -Wno-deprecated -Wno-deprecated-declarations -std=gnu99 -O3" LDFLAGS="-lpthread" CC=oshcc --disable-mpi --enable-quick-tests --enable-active-sets --disable-error
make clean
make install
make oshmem_test

cat > sos-mlnx.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH
export BENCH_HOME=$JENKINS_HOME/deps/downloads/tests-mellanox

oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=atomic
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:start
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:whoami
## conflicting semantic for shmem_align with a non-power of two:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:shmalloc
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:get
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:put
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:barrier
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:static
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:heap
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=basic:fence
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=coll
## Unresolved occasional failures probably due to memory mismanagement/corruption:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=data
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=lock
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=reduce
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:barrier_all
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:wait
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:wait_until
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:barrier
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:quiet
## stress tests are not functional tests, and they consumes too much time:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:barrier_stress
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=sync:fence_stress
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=stride
## mix test is SOS with MPI, which requires an MPI lib:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=mix
## analysis test is not a functional test, and it consumes too much time:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=analysis
oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=nbi
## tests opal_progress() function, which SOS does not have:
#- oshrun -np 4 $BENCH_HOME/install/bin/oshmem_test exec --task=misc
make clean

EOF

chmod +x sos-mlnx.sh
salloc -J "MLNX-SOS" -N 1 -t 30 ./sos-mlnx.sh

# Cray

cat > sos-cray.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH
cp -r $JENKINS_HOME/deps/downloads/tests-cray $WORKSPACE/
export CRAY_TESTS_DIR=$WORKSPACE/tests-cray
export SOS_DISABLE_FORTRAN

$SOS_SRC/scripts/cray_tests.sh

EOF

chmod +x sos-cray.sh
salloc -J "CRAY-SOS" -N 1 -t 30 ./sos-cray.sh

# UH

cp -r $JENKINS_HOME/deps/downloads/tests-uh $WORKSPACE/

cat > sos-uh.sh << "EOF"
#!/bin/bash

set -x
export PATH=$SOS_INSTALL/bin:$DEP_BUILD_DIR/hydra/bin:$BASE_PATH
export BENCH_HOME=$WORKSPACE/tests-uh
export PAR_MAKE="-j 4"
export SOS_DISABLE_FORTRAN
cd $BENCH_HOME

make $PAR_MAKE C_feature_tests
make C_feature_tests-run 2>&1 | tee uh-tests-c-feature-tests.log

# Check for failures in the C tests
if grep "^(test_[0-9]\+) Running.*Failed$" uh-tests-c-feature-tests.log; then false; else true; fi

if [ -z "$SOS_DISABLE_FORTRAN" ]; then
    make F_feature_tests;
    if [ $? -eq 0 ]; then
        make F_feature_tests-run 2>&1 | tee uh-tests-f-feature-tests.log;
        # Check for failures in the Fortran tests
        if grep "^(test_[0-9]\+) Running.*Failed$" uh-tests-f-feature-tests.log; then false; else true; fi;
    fi;
fi
      
if [ -z "$SOS_DISABLE_FORTRAN" ]; then
    make F_error_tests;
    if [ $? -eq 0 -a $SOS_ENABLE_ERROR_TESTS -eq 1 ]; then
        make F_error_tests-run 2>&1 | tee uh-tests-f-error-tests.log;
        # Check for failures in the Fortran tests
        if grep "^(test_[0-9]\+) Running.*Failed$" uh-tests-f-error-tests.log; then false; else true; fi;
    fi;
fi
make clean

EOF

chmod +x sos-uh.sh
salloc -J "UH-SOS" -N 1 -t 30 ./sos-uh.sh
