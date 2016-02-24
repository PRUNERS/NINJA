include ../Makefile.config

SRC_DIR=.
SUB_DIR_ENC=

nin_SRCS =    $(SRC_DIR)/nin.cpp 
nin_OBJS = 	$(nin_SRCS:%.cpp=%.o) 
nin_DEPS = 	$(nin_SRCS:%.cpp=%.d) 

nin_o_LIBS =	../lib/libnin.o
nin_a_LIBS =	../lib/libnin.a 
nin_so_LIBS =	../lib/libnin.so	

OBJS = $(nin_OBJS)
DEPS = $(nin_DEPS)
LIBS = $(nin_so_LIBS) $(nin_a_LIBS) 

#$@: target name
#$<: first dependent file
#$^: all indemendent files


all:  $(LIBS)

$(nin_a_LIBS):  $(nin_OBJS) 
	ld    -o $(nin_o_LIBS) -r $(nin_OBJS)
	ar cr $@ $(nin_a_LIBS) 
	ranlib $@


$(nin_so_LIBS): $(nin_a_LIBS) $(nin_OBJS) 
	$(CC) $(LDFLAGS) $(LDLIBS) -g -shared -o $@ $(nin_o_LIBS)


.SUFFIXES: .c .o
.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -c -MMD -MP $< 
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -o $@ -c $< 

install: all
	install ../lib/libnin.a  /g/g90/sato5/opt/lib/
	install ../lib/libnin.so /g/g90/sato5/opt/lib/


.SUFFIXES: .cpp .o
.cpp.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -c -MMD -MP $<
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) $(LDLIBS) -o $@ -c $< 


.PHONY: clean
clean:
	-rm -rf $(OBJS) $(DEPS) $(LIBS) $(nin_o_LIBS)

.PHONY: clean_core
clean_core:
	-rm -rf *.core

