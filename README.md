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
    
### Run without NINJA
This example code (ninja_test_matching_race) is a synthetic benchmark embracing a message-race bug. 

    $ ./ninja_test_matching_race
    Usage: ./matching_race <type: 0=SR, 1=SSR> <matching safe: 0=unsafe 1=safe> <# of loops> <# of patterns per loop> <interval(usec)>
    
* `<type: 0=SR, 1=SSR>`: 
    * `0`: TEST CASE 0 (Same as Case 1 in reference)
    * `1`: TEST CASE 1 (Same as Case 2 in reference)
* `<matching safe: 0=unsafe 1=safe>`
    * `0`: Run under tag-unsafe condition (i.e., enable message races)
    * `1`: Run under tag-safe condition (i.e., disable message races)
* `<# of loops>`: the number of iterations
* `<# of patterns per loop>`: the number of communication routines per iteration
* `<interval(usec)>`: interval time (usec) between communication routines
    
Manifestation of this bug is non-deterministic. Even if enabling message races (i.e. `<matching safe>=0`), this message-race bug may not manifest and run all the way to loop 1000. (NOTE: this bug is non-deterministic. Therefore, you will not see the same manifestations as well as outputs)

    $ srun -n (OR mpirun -np) 16 ./ninja_test_matching_race 1 0 1000 2 0
    ***************************
    Matching Type     : 1
    Is Matching safe ?: 0
    # of Loops        : 1000
    # of Patterns     : 2
    Interval(usec)    : 0
    ***************************
    NIN(test):  0: loop: 1 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 2 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 3 (ninja_test_matching_race.c:225)
     ...
    NIN(test):  0: loop: 1000 (ninja_test_matching_race.c:225)
    NIN(test):  0: Time: 0.054226 (ninja_test_matching_race.c:314)
 
### Run under System-centric mode
If the bug does not manifest, NINJA's system-centric mode may be able to manifest the bug.
    
    $ LD_PRELOAD=<path to installation directory>/lib/libninja.so NIN_PATTERN=2 NIN_MODEL_MODE=0 srun -n (OR mpirun -np) 16 ./ninja_test_matching_race 1 0 1000 2 0 
    ===========================================
     NIN_LOCAL_NOISE: 0
     NIN_PATTERN: 2
     NIN_MODEL_MODE: 0
     NIN_DIR: ./.ninja
    ===========================================
    ***************************
    Matching Type     : 1
    Is Matching safe ?: 0
    # of Loops        : 1000
    # of Patterns     : 2
    Interval(usec)    : 0
    ***************************
    NIN(test):  0: loop: 1 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 2 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 3 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 4 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 5 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 6 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 7 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 8 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 9 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 10 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 11 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 12 (ninja_test_matching_race.c:225)
    NIN(test): 10: Wrong matching detected: recv_val: 23, send_val: 22 at loop 11 (ninja_test_matching_race.c:247)
    application called MPI_Abort(, 0) - process 10
    
NINJA's system-centric mode simply emulates noisy enviroments to induce message races. Therefore, if two unsafe communication routines are significantly separated. NINJA's system-centric mode may not be able to manifest message-race bugs.
Let's increate interval time between unsafe communication routines from 0 to 1000 usec.

    $ LD_PRELOAD=<path to installation directory>/lib/libninja.so NIN_PATTERN=2 NIN_MODEL_MODE=0 srun -n (OR mpirun -np) 16 ./ninja_test_matching_race 1 0 1000 2 1000
    ***************************
    Matching Type     : 1
    Is Matching safe ?: 0
    # of Loops        : 1000
    # of Patterns     : 2
    Interval(usec)    : 1000
    ***************************
    NIN(test):  0: loop: 1 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 2 (ninja_test_matching_race.c:225)
    NIN(test):  0: loop: 3 (ninja_test_matching_race.c:225)
     ...
    NIN(test):  0: loop: 1000 (ninja_test_matching_race.c:225)
    NIN(test):  0: Time: 2.433113 (ninja_test_matching_race.c:314)
    NIN:0: Learning file written to ./.ninja directory (ninj_fc.cpp:566)

During NINJA's system-centric mode, NINJA profiles intervals of each unsafe communication routine. At the end of the execution (on MPI_Finalize()) under NINJA's system-centric mode, NINJA outputs the profile for NINJA's application-centric mode.

### Run under Application-centric mode
NINJA's application-cenric mode reads this profile, then injects adequate amounts of noise in order to manifest message-race bugs.

    $ LD_PRELOAD=<path to installation directory>/lib/libninja.so NIN_PATTERN=2 NIN_MODEL_MODE=1 srun -n (OR mpirun -np) 16 ./ninja_test_matching_race 1 0 1000 2 1000
    ===========================================
     NIN_LOCAL_NOISE: 0
     NIN_PATTERN: 2
     NIN_MODEL_MODE: 1
     NIN_DIR: ./.ninja
    ===========================================
    ***************************
    Matching Type     : 1
    Is Matching safe ?: 0
    # of Loops        : 1000
    # of Patterns     : 2
    Interval(usec)    : 1000
    ***************************
    NIN(test):  0: loop: 1 (ninja_test_matching_race.c:225)
    NIN(test): 13: Wrong matching detected: recv_val: 1, send_val: 0 at loop 0 (ninja_test_matching_race.c:247)
    application called MPI_Abort(, 0) - process 13


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
