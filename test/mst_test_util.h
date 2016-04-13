#ifndef NIN_TEST_UTIL_H
#define NIN_TEST_UTIL_H


#define mst_test_dbg_print(format, ...) \
  do { \
  fprintf(stderr, "NIN(test):%3d: " format " (%s:%d)\n", my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } while (0)

#define mst_test_dbgi_print(rank, format, ...) \
  do { \
  if (rank == 0) { \
  fprintf(stderr, "NIN(test):%3d: " format " (%s:%d)\n", my_rank, ## __VA_ARGS__, __FILE__, __LINE__); \
  } \
  } while (0)




double get_time(void);
int init_rand(int seed);
int init_ndrand();
int get_rand(int max);
int get_hash(int original_val, int max);
void init_noise();
void do_work(int usec);

#endif
