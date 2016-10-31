# Introduction

 * Ninja (Noise INJection Agent) is a tool for reproducing subtle and unintended mesage races

# Quick Start

## Get source code

## Build Ninja

 use mvapich2-intel-2.1
 build hypre
 build ex5
 build ninja

## Run examples

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
     * `2`: Constant local noise
       * `NIN_LOCAL_NOISE_AMOUNT`: Run CPU intensive work for <NIN_LOCAL_NOISE_AMOUN> usec after send/recv/matching function

