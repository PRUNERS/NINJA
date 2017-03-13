# Assuming foo is a program or library:
#  foo_CFLAGS:   Additional C compiler flags
#  foo_CPPFLAGS: Additional preprocessor flags (-Is and -Ds)
#  foo_LDADD:    Additional link objects, -ls and -Ls (if foo is a program)
#  foo_LIBADD:   Additional link objects, -ls and -Ls (if foo is a library) 
#  foo LDFLAGS:  Additional linker flags
# The default value for foo_XXXFLAGS is $(AM_XXXFLAGS).

AM_CFLAGS = -fPIC -std=c++0x -pthread -g -lstdc++
AM_CXXFLAGS = $(AM_CFLAGS)

libninja_la_LIBRARY = libninja.la
lib_LTLIBRARIES = $(libninja_la_LIBRARY) 

# ===== libninja =====
libninja_la_SOURCES =   ninj.cpp \
                ninj_fc.cpp \
                ninj_thread.cpp \
                nin_util.cpp \
                nin_mpi_util.cpp \
                mst_io.cpp



