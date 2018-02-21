#!/bin/sh

COMPILER=$Compiler
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
cd $SOS_SRC
mkdir ofi-build
cd ofi-build
../configure --with-ofi=$JENKINS_INSTALL/libfabric/ --prefix=$JENKINS_INSTALL/sandia-shmem-ofi $SOS_GLOBAL_BUILD_OPTS $SOS_BUILD_OPTS
make $PAR_MAKE
make $PAR_MAKE check TESTS=
make VERBOSE=1 TEST_RUNNER="mpiexec.hydra -np 2" check
make install


if [ "$CC" = "icc" ]; then
	'[[ ! -z "${INTEL_INSTALL_PATH}" ]] && uninstall_intel_software'
fi
