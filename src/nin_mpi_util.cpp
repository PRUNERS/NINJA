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

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

#include "nin_mpi_util.h"
#include "nin_util.h"


using namespace std;

MPI_Status *nin_status_allocate(int incount, MPI_Status *statuses, int *flag)
{
  MPI_Status *new_statuses;
  if (statuses == NULL || statuses == MPI_STATUS_IGNORE) {
    new_statuses = (MPI_Status*)malloc(incount * sizeof(MPI_Status));
    *flag  = 1;
  } else {
    new_statuses = statuses;
    *flag = 0;
  }

  // for (int i = 0; i < incount; i++) {
  //   NIN_DBG(" start  waitall: src: %d, tag: %d count: %d", new_statuses[i].MPI_SOURCE, new_statuses[i].MPI_TAG, incount);
  // }

  for (int i = 0; i < incount; i++) {
    new_statuses[i].MPI_SOURCE = MPI_ANY_SOURCE;
    new_statuses[i].MPI_TAG    = MPI_ANY_TAG;
  }

  // for (int i = 0; i < incount; i++) {
  //   NIN_DBG(" end  waitall: src: %d, tag: %d count: %d", new_statuses[i].MPI_SOURCE, new_statuses[i].MPI_TAG, incount);
  // }
  return new_statuses;
}

void nin_status_free(MPI_Status *statuses)
{
  free(statuses);
  return;
}
