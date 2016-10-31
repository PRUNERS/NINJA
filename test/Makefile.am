SUBDIRS = ../src

noinst_PROGRAMS = \
	ninja_test_pingpong \
	ninja_test_units \
	ninja_test_hypre_parasails \
	ninja_test_matching_race

ninja_test_pingpong_SOURCES  = ninja_test_pingpong.c ninja_test_util.c
ninja_test_units_SOURCES  = ninja_test_units.c ninja_test_util.c
ninja_test_hypre_parasails_SOURCES  = ninja_test_hypre_parasails.c ninja_test_util.c
ninja_test_matching_race_SOURCES  = ninja_test_matching_race.c ninja_test_util.c


.PHONY: clean_core
clean_core:
	-rm -f *.core
	-rm -f *.btr

