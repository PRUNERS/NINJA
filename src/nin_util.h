/* ==========================NINJA:LICENSE==========================================   
  Copyright (c) 2016, Lawrence Livermore National Security, LLC.                     
  Produced at the Lawrence Livermore National Laboratory.                            
                                                                                    
  Written by Kento Sato, kento@llnl.gov. LLNL-CODE-713637.                           
  All rights reserved.                                                               
                                                                                    
  This file is part of NINJA. For details, see https://github.com/PRUNER/NINJA      
  Please also see the LICENSE.TXT file for our notice and the LGPL.                      
                                                                                    
  This program is free software; you can redistribute it and/or modify it under the 
  terms of the GNU General Public License (as published by the Free Software         
  Foundation) version 2.1 dated February 1999.                                       
                                                                                    
  This program is distributed in the hope that it will be useful, but WITHOUT ANY    
  WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or                  
  FITNESS FOR A PARTICULAR PURPOSE. See the terms and conditions of the GNU          
  General Public License for more details.                                           
                                                                                    
  You should have received a copy of the GNU Lesser General Public License along     
  with this program; if not, write to the Free Software Foundation, Inc., 59 Temple 
  Place, Suite 330, Boston, MA 02111-1307 USA                                 
  ============================NINJA:LICENSE========================================= */

#ifndef _NIN_H_
#define _NIN_H_

extern int nin_my_rank;


#if MPI_VERSION == 1 || MPI_VERSION == 2
#define nin_mpi_const 
#else
#define nin_mpi_const const
#endif

#define NIN_DBG(format, ...)		\
  do { \
    fprintf(stderr, "NIN:%d: " format " (%s:%d)\n", nin_my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)

#define NIN_DBGI(rank, format, ...)	\
  do { \
    if (nin_my_rank == rank) fprintf(stderr, "NIN:%d: " format " (%s:%d)\n", nin_my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)


void NIN_Init();
double NIN_Wtime();
double NIN_get_time();
int NIN_init_ndrand();
int NIN_init_rand(int seed);
int NIN_get_rand(int max);
void NIN_do_work(int usec);

#endif

