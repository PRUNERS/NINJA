# Introduction

 * Ninja (Noise INJection Agent) is a tool for reproducing subtle and unintended mesage races

# Quick Start

## Build Ninja


    autogen.py
    configure --prefix=\<path to installation directory\>

## Run examples

   LD_PRELOAD=<path to installation directory>/lib/libninja.so srun -n X a.out

# Environmental valiables

 * `NIN_PATTERN`: 
     * `0`: Network noise free
     * `1`: Random network noise
       * `NIN_RAND_RATIO`: NIN_RAND_RATIO % of MPI sends are delayed
       * `NIN_RAND_DELAY`: Selected messages are delayed by usleep(NIN_RAND_DELAY)
     * `2`: Smart network noise injection
       * `NIN_MODEL_MODE`
       	 * `0`: Passive mode
       	 * `1`: Active mode
       * `NIN_DIR`: Directory for send pattern learning files
 * `NIN_LOCAL_NOISE`:
     * `0`: Local noise free
     * `1`: Constant local noise
       * `NIN_LOCAL_NOISE_AMOUNT`: Run CPU intensive work for <NIN_LOCAL_NOISE_AMOUN> usec after send/recv/matching function

# Reference
Kento Sato, Dong H. Ahn, Ignacio Laguna, Gregory L. Lee, Martin Schulz and Christopher M. Chambreau, “Noise Injection Techniques for Reproducing Subtle and Unintended Message Races”, Proceedings of the 20th ACM SIGPLAN Symposium on Principles and Practice of Parallel Programming (PPoPP17), Austin, USA, Feb, 2017.
