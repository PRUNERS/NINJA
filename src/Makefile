include ../Makefile.config

SRC_DIR=.
SUB_DIR_ENC=
LIB_DIR=../lib/
BIN_DIR=../bin/
INSTALL_LIB_DIR=/g/g90/sato5/opt/lib/
INSTALL_BIN_DIR=/g/g90/sato5/opt/bin/

# ====== library ==========
mst_SRCS =    $(SRC_DIR)/mst.cpp \
		$(SRC_DIR)/mst_io.cpp
mst_OBJS = 	$(mst_SRCS:%.cpp=%.o) 
mst_DEPS = 	$(mst_SRCS:%.cpp=%.d) 
mst_a_LIBS =	libmst.a 
mst_so_LIBS =	libmst.so

mor_SRCS =    $(SRC_DIR)/mor.cpp \
		$(SRC_DIR)/nin_util.cpp \
		$(SRC_DIR)/nin_mpi_util.cpp \
		$(SRC_DIR)/mst_io.cpp 
mor_OBJS = 	$(mor_SRCS:%.cpp=%.o) 
mor_DEPS = 	$(mor_SRCS:%.cpp=%.d) 
mor_a_LIBS =	libmor.a 
mor_so_LIBS =	libmor.so

ninj_SRCS =    $(SRC_DIR)/ninj.cpp \
		$(SRC_DIR)/ninj_fc.cpp \
		$(SRC_DIR)/ninj_thread.cpp \
		$(SRC_DIR)/nin_util.cpp \
		$(SRC_DIR)/nin_mpi_util.cpp \
		$(SRC_DIR)/mst_io.cpp 
ninj_OBJS = 	$(ninj_SRCS:%.cpp=%.o) 
ninj_DEPS = 	$(ninj_SRCS:%.cpp=%.d) 
ninj_a_LIBS =	libninj.a 
ninj_so_LIBS =	libninj.so


# ====== binary ==========
nnm_SRCS =    $(SRC_DIR)/nnm.cpp \
		$(SRC_DIR)/nin_util.cpp \
		$(SRC_DIR)/mst_io.cpp
nnm_OBJS = 	$(nnm_SRCS:%.cpp=%.o) 
nnm_DEPS = 	$(nnm_SRCS:%.cpp=%.d) 	
nnm_PROGRAM  = nnm

PROGRAMS = $(nnm_PROGRAM)
OBJS = $(mst_OBJS) $(mor_OBJS) $(ninj_OBJS) $(nnm_OBJS) 
DEPS = $(mst_DEPS) $(mor_DEPS) $(ninj_DEPS) $(nnm_DEPS)
LIBS = $(mst_so_LIBS) $(mst_a_LIBS) $(mor_so_LIBS) $(mor_a_LIBS) $(ninj_so_LIBS) $(ninj_a_LIBS) 

#$@: target name
#$<: first dependent file
#$^: all indemendent files


all: $(LIBS) $(PROGRAMS)

-include $(DEPS)

$(mst_a_LIBS):  $(mst_OBJS) 
	ar cr $(LIB_DIR)/$@ $(mst_OBJS) 
	ranlib $(LIB_DIR)/$@
#	ld    -o $(mst_o_LIBS) -r $(mst_OBJS)
#	ar cr $@ $(mst_a_LIBS) 
#	ranlib $@

$(mst_so_LIBS): $(mst_OBJS) 
	$(CC) $(LDFLAGS) $(LDLIBS) -g -shared -o $(LIB_DIR)/$@ $(mst_OBJS)

$(mor_a_LIBS):  $(mor_OBJS) 
	ar cr $(LIB_DIR)/$@ $(mor_OBJS) 
	ranlib $(LIB_DIR)/$@

$(mor_so_LIBS): $(mor_OBJS) 
	$(CC) $(LDFLAGS) $(LDLIBS) -g -shared -o $(LIB_DIR)/$@ $(mor_OBJS)

$(ninj_a_LIBS):  $(ninj_OBJS) 
	ar cr $(LIB_DIR)/$@ $(ninj_OBJS) 
	ranlib $(LIB_DIR)/$@

$(ninj_so_LIBS): $(ninj_OBJS) 
	$(CC) $(LDFLAGS) $(LDLIBS) -g -shared -o $(LIB_DIR)/$@ $(ninj_OBJS)


$(nnm_PROGRAM): $(nnm_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) ${LDLIBS} -o $(BIN_DIR)/$@ $^


.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -c -MMD -MP $< 
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -o $@ -c $< 

.SUFFIXES: .cpp .o
.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -c -MMD -MP $<
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -o $@ -c $< 

install: all
	for lib in $(LIBS);     do ( install $(LIB_DIR)/$$lib  $(INSTALL_LIB_DIR)/$$lib ); done
	for bin in $(PROGRAMS); do ( install $(BIN_DIR)/$$bin  $(INSTALL_BIN_DIR)/$$bin ); done
#	install ../lib/libmst.a   /g/g90/sato5/opt/lib/
#	install ../lib/libmst.so  /g/g90/sato5/opt/lib/

uninstall: 
	-for lib in $(LIBS);     do ( rm  $(INSTALL_LIB_DIR)/$$lib ); done
	-for bin in $(PROGRAMS); do ( rm  $(INSTALL_BIN_DIR)/$$bin ); done

.PHONY: clean
clean:
	-for lib in $(LIBS);     do ( rm  $(LIB_DIR)/$$lib ); done
	-for bin in $(PROGRAMS); do ( rm  $(BIN_DIR)/$$bin ); done
	-rm -rf $(OBJS) $(DEPS) $(LIBS) $(PROGRAMS) 

.PHONY: cleanc
cleanc:
	-rm -rf *.core

