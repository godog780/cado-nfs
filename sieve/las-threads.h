#ifndef LAS_THREADS_H_
#define LAS_THREADS_H_

#include "las-forwardtypes.h"
#include "bucket.h"
#include "fb.h"
#include "las-report-stats.h"
#include "las-base.hpp"

/* {{{ thread-related defines */
/* All of this exists _for each thread_ */
struct thread_side_data_s : private NonCopyable {
  m_bucket_array_t mBA; /* Not used if not fill_in_m_buckets (3 passes sort) */
  k_bucket_array_t kBA; /* Ditto for fill_in_k_buckets (2 passes sort) */
  bucket_array_t BA;    /* Always used */
  const fb_part *fb;

  /* For small sieve */
  int * ssdpos;
  int * rsdpos;

  unsigned char *bucket_region;
  sieve_checksum checksum_post_sieve;

  thread_side_data_s();
  ~thread_side_data_s();
  void set_fb(const fb_part *_fb) {fb = _fb;}
};
typedef struct thread_side_data_s thread_side_data[1];
typedef struct thread_side_data_s * thread_side_data_ptr;
typedef const struct thread_side_data_s * thread_side_data_srcptr;

struct thread_data_s : private NonCopyable {
  int id;
  thread_side_data sides[2];
  las_info_srcptr las;
  sieve_info_ptr si;
  las_report rep;
  unsigned char *SS;
  bool is_initialized;
  thread_data_s();
  ~thread_data_s();
  void init(int id, las_info_srcptr las);
  void pickup_si(sieve_info_ptr si);
};
typedef struct thread_data_s *thread_data_ptr;
typedef const struct thread_data_s *thread_data_srcptr;

void thread_do(thread_data_ptr, void * (*) (thread_data_ptr), int);
thread_data_ptr thread_data_alloc(las_info_ptr las, int n);
void thread_data_free(thread_data_ptr thrs);
void thread_pickup_si(thread_data_ptr thrs, sieve_info_ptr si, int n);
void thread_buckets_alloc(thread_data_ptr thrs, unsigned int n);
void thread_buckets_free(thread_data_ptr thrs, unsigned int n);
double thread_buckets_max_full(thread_data_ptr thrs, int n);

#endif
