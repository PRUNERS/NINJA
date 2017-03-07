#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
# https://www.gnu.org/software/autoconf/manual/autoconf-2.60/html_node/index.html#Top

AC_PREREQ([2.63])
AC_INIT([NINJA],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']), 
	[kento@llnl.gov])

AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign -Wall -Werror], [0.1])

### Action Macros
m4_ifdef([AM_PROG_AR], [AM_PROG_AR])
AC_SUBST([LIB_CURRENT], [0])
AC_SUBST([LIB_REVISION], [1])
AC_SUBST([LIB_AGE], [0])



#### Checks for programs.
AC_LANG_PUSH(C)
LX_FIND_MPI
test "x$have_C_mpi" = xyes || AC_MSG_ERROR([Failed to find C MPI Wrapper.])
AC_LANG_POP()
AC_LANG_PUSH(C++)
LX_FIND_MPI
test "x$have_CXX_mpi" = xyes || AC_MSG_ERROR([Failed to find C++ MPI Wrapper.])
AC_LANG_POP()
AC_LANG_PUSH(Fortran)
LX_FIND_MPI
test "x$have_F_mpi" = xyes || AC_MSG_ERROR([Failed to find Fortran MPI Wrapper.])
AC_LANG_POP()

CC="$MPICC"
CXX="$MPICXX"
FC="$MPIFC"
AC_CHECK_PROGS([MAKE], [make], [:])
if test "$MAKE" = :; then
   AC_MSG_ERROR([This package needs make.])
fi
AC_PROG_LIBTOOL

#### Checks for libraries.
mpi_version3="yes"
AC_CHECK_LIB([mpi], [PMPI_Ibarrier], [], [mpi_version3="no"], ["$MPI_CLDFLAGS"])
if test "$mpi_version3" = "no"; then
   AC_MSG_ERROR([This package needs MPI-3.])
fi

#### Checks for header files.

#### Checks for typedefs, structures, and compiler characteristics.

#### Checks for library functions.

#### Output files.

AC_CONFIG_HEADERS(config.h)
AC_CONFIG_FILES([
	Makefile 
	src/Makefile 
	test/Makefile])
AC_OUTPUT
