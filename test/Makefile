include ../Makefile.config

nin_test_pingpong_SRCS = nin_test_pingpong.c nin_test_util.c
nin_test_pingpong_OBJS = $(nin_test_pingpong_SRCS:%.c=%.o)
nin_test_pingpong_DEPS = $(nin_test_pingpong_SRCS:%.c=%.d)
nin_test_pingpong_PROGRAM = nin_test_pingpong

nin_test_hypre_parasails_SRCS = nin_test_hypre_parasails.c nin_test_util.c
nin_test_hypre_parasails_OBJS = $(nin_test_hypre_parasails_SRCS:%.c=%.o)
nin_test_hypre_parasails_DEPS = $(nin_test_hypre_parasails_SRCS:%.c=%.d)
nin_test_hypre_parasails_PROGRAM = nin_test_hypre_parasails

PROGRAMS= $(nin_test_hypre_parasails_PROGRAM) \
	 $(nin_test_pingpong_PROGRAM)
OBJS= $(nin_test_pingpong_OBJS) \
	 $(nin_test_hypre_parasails_OBJS) \
DEPS= $(nin_test_pingpong_DEPS) \
	$(nin_test_hypre_parasails_DEPS) 

#$@: target name
#$<: first dependent file
#$^: all indemendent files

all: src_make $(PROGRAMS) 

-include $(DEPS)

src_make:
	@$(MAKE) -C ../src

install: all
	@$(MAKE) -C ../src install

$(nin_test_pingpong_PROGRAM): $(nin_test_pingpong_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(nin_test_hypre_parasails_PROGRAM): $(nin_test_hypre_parasails_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^


.SUFFIXES: .c .o
.c.o: 
	$(CC) $(CFLAGS) $(LDFLAGS) -c -MMD -MP $<
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ -c $< 

.SUFFIXES: .cpp .o
.cpp.o: 
	$(CC) $(CXXFLAGS) $(LDFLAGS) -c -MMD -MP $<
	$(CC) $(CXXFLAGS) $(LDFLAGS) -o $@ -c $< 


.PHONY: clean 
clean:
	@$(MAKE) clean -C ../src
	-rm -f $(PROGRAMS) $(OBJS) $(DEPS)

.PHONY: clean_core
cleanc:
	-rm -f *.core
	-rm -f *.btr


.PHONY: clean_all
clean_all: clean clean_core clean_record clean_btr


# DO NOT DELETE
