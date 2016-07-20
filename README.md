# Introduction

 * Ninja (Noise INJection Agent) is a tool for reproducing subtle and unintended mesage races

# Quick Start

## Get source code

## Build Ninja

## Build test code: MCB

## Run examples

# Environmental valiables

 * `NIN_PATTERN`: 
     * `0`: Noise free
     * `1`: Random noise
       * `NIN_RAND_RATIO`: NIN_RAND_RATIO % of MPI sends are delayed
       * `NIN_RAND_DELAY`: Selected messages are delayed by usleep(NIN_RAND_DELAY)
     * `2`: Smart noise injection
       * `NIN_MODEL_MODE`
       	 * `0`: Passive mode
       	 * `1`: Active mode
       * `NIN_DIR`: Directory for send pattern learning files
