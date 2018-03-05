#!/bin/sh

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

# Build SOS
cd $SOS_SRC
./autogen.sh
export BASE_PATH=$PATH

cd $SOS_SRC
export PATH=$JENKINS_INSTALL/hydra/bin:$BASE_PATH
./configure --with-ofi=$JENKINS_INSTALL/libfabric/ --prefix=$JENKINS_INSTALL/sandia-shmem-ofi $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
make $PAR_MAKE
make install

###
### Run ISx (OFI)
###

export PATH=$JENKINS_INSTALL/sandia-shmem-ofi/bin:$JENKINS_INSTALL/hydra/bin:$BASE_PATH 
export OSHRUN_LAUNCHER="mpiexec.hydra"
cd $JENKINS_SRC/ISx/SHMEM
make CC=oshcc LDLIBS=-lm
export BENCH_HOME=$JENKINS_SRC/ISx/SHMEM
# Note: This SHMEM_SYMMETRIC_SIZE setting may exceed the memory available in the CI testing environment
cat > job.sh << "EOF"
#!/bin/bash

set -x
export PATH=$JENKINS_INSTALL/sandia-shmem-ofi/bin:$JENKINS_INSTALL/hydra/bin:$BASE_PATH

oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.strong 134217728 output_strong
oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.weak 33554432 output_weak
oshrun -np 4 -env SHMEM_SYMMETRIC_SIZE '4G' $BENCH_HOME/bin/isx.weak_iso 33554432 output_weak_iso
make clean

EOF

chmod +x job.sh
salloc -J "ISx-SOS" -N 1 -t 30 ./job.sh

