include ../Makefile.config

mst_test_pingpong_SRCS = mst_test_pingpong.c mst_test_util.c
mst_test_pingpong_OBJS = $(mst_test_pingpong_SRCS:%.c=%.o)
mst_test_pingpong_DEPS = $(mst_test_pingpong_SRCS:%.c=%.d)
mst_test_pingpong_PROGRAM = mst_test_pingpong

mst_test_hypre_parasails_SRCS = mst_test_hypre_parasails.c mst_test_util.c
mst_test_hypre_parasails_OBJS = $(mst_test_hypre_parasails_SRCS:%.c=%.o)
mst_test_hypre_parasails_DEPS = $(mst_test_hypre_parasails_SRCS:%.c=%.d)
mst_test_hypre_parasails_PROGRAM = mst_test_hypre_parasails

mst_test_units_SRCS = mst_test_units.c mst_test_util.c
mst_test_units_OBJS = $(mst_test_units_SRCS:%.c=%.o)
mst_test_units_DEPS = $(mst_test_units_SRCS:%.c=%.d)
mst_test_units_PROGRAM = mst_test_units

nin_test_matching_race_SRCS = nin_test_matching_race.c mst_test_util.c
nin_test_matching_race_OBJS = $(nin_test_matching_race_SRCS:%.c=%.o)
nin_test_matching_race_DEPS = $(nin_test_matching_race_SRCS:%.c=%.d)
nin_test_matching_race_PROGRAM = nin_test_matching_race

PROGRAMS= $(mst_test_pingpong_PROGRAM) \
	 $(mst_test_units_PROGRAM) \
	 $(mst_test_hypre_parasails_PROGRAM) \
	 $(nin_test_matching_race_PROGRAM) \
OBJS= $(mst_test_pingpong_OBJS) \
	 $(mst_test_hypre_parasails_OBJS) \
	 $(mst_test_units_OBJS) \
	 $(nin_test_matching_race_OBJS) 
DEPS= $(mst_test_pingpong_DEPS) \
	$(mst_test_hypre_parasails_DEPS) \
	$(mst_test_units_DEPS) \
	$(nin_test_matching_race_DEPS) 

#$@: target name
#$<: first dependent file
#$^: all indemendent files

all: src_make $(PROGRAMS) 

-include $(DEPS)

src_make:
	@$(MAKE) -C ../src

install: all
	@$(MAKE) -C ../src install
	@$(MAKE) -C ../scripts install

$(mst_test_pingpong_PROGRAM): $(mst_test_pingpong_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(mst_test_hypre_parasails_PROGRAM): $(mst_test_hypre_parasails_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(mst_test_units_PROGRAM): $(mst_test_units_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(nin_test_matching_race_PROGRAM): $(nin_test_matching_race_OBJS)
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
