<img src="files/NINJA_logo.png" height="60%" width="60%" alt="ReMPI logo" title="ReMPI" align="middle" />

# Introduction

 * Ninja (Noise INJection Agent) is a tool for reproducing subtle and unintended mesage races

# Quick start

## 1. Build NINJA 

### From Spack (Spack also builds ReMPI)

    $ git clone https://github.com/LLNL/spack
    $ spack/bin/spack install rempi

### From git repogitory

    $ git clone git@github.com:PRUNERS/ReMPI.git
    $ cd <rempi directory>
    $ ./autogen.sh
    $ configure --prefix=<path to installation directory>
    $ make
    $ make install

### From tarball

    $ tar zxvf ./rempi_xxxxx.tar.bz
    $ cd <rempi directory>
    $ configure --prefix=<path to installation directory>
    $ make
    $ make install
    
## 2. Run examples

    $ cd test
    $ mkdir .ninja
    
### Run without NINJA
This example code contains message races bug, but the bug may or may not manifest.

    $ mpirun -n 4 ./ninja_test_hypre_parasails 10
    NIN(test):  0: loop 0 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 1 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 2 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 3 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 4 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 5 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 6 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 7 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 8 (ninja_test_hypre_parasails.c:724)
    NIN(test):  0: loop 9 (ninja_test_hypre_parasails.c:724)
    Time: 10.001586

### System-centric mode
This system-centric mode will manifest the bug.
    
    $ NIN_PATTERN=2 NIN_MODEL_MODE=0 NIN_DIR=./.ninja NIN_LOCAL_NOISE=0 LD_PRELOAD=<path to installation directory>/lib/libninja.so srun(or mpirun) -n 4 ./ninja_test_units matching
    
### Application-centric mode
This application-cenric mode will more quickly and frequently manifest the bug.

    $ NIN_PATTERN=2 NIN_MODEL_MODE=1 NIN_DIR=./.ninja NIN_LOCAL_NOISE=0 LD_PRELOAD=<path to installation directory>/lib/libninja.so srun(or mpirun) -n 4 ./ninja_test_units matching

# Environment variables

 * `NIN_PATTERN`: 
     * `0`: Network noise free
     * `1`: Random network noise
       * `NIN_RAND_RATIO`: NIN_RAND_RATIO % of MPI sends are delayed
       * `NIN_RAND_DELAY`: Selected messages are delayed by usleep(NIN_RAND_DELAY)
     * `2`: Smart network noise injection
       * `NIN_MODEL_MODE`
       	 * `0`: System-centric mode
       	 * `1`: Application-centric mode
       * `NIN_DIR`: Directory for send pattern learning files
 * `NIN_LOCAL_NOISE`:
     * `0`: Local noise free
     * `1`: Constant local noise
       * `NIN_LOCAL_NOISE_AMOUNT`: Run CPU intensive work for <NIN_LOCAL_NOISE_AMOUN> usec after send/recv/matching function

# Reference
Kento Sato, Dong H. Ahn, Ignacio Laguna, Gregory L. Lee, Martin Schulz and Christopher M. Chambreau, “Noise Injection Techniques for Reproducing Subtle and Unintended Message Races”, Proceedings of the 20th ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP17), Austin, USA, Feb, 2017.
