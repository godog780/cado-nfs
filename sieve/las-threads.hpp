#ifndef LAS_THREADS_HPP_
#define LAS_THREADS_HPP_

#include <pthread.h>
#include <algorithm>
#include <vector>
#include "threadpool.hpp"
#include "las-forwardtypes.hpp"
#include "bucket.hpp"
#include "fb.hpp"
#include "las-report-stats.hpp"
#include "las-base.hpp"
#include "tdict.hpp"

class thread_workspaces;

struct thread_data;

/* All of this exists _for each thread_ */
struct thread_side_data : private NonCopyable {

  /* For small sieve */
  std::vector<spos_t> ssdpos;
  std::vector<spos_t> rsdpos;

  /* The real array where we apply the sieve.
     This has size BUCKET_REGION_0 and should be close to L1 cache size. */
  unsigned char *bucket_region = NULL;
  sieve_checksum checksum_post_sieve;

  thread_side_data();
  ~thread_side_data();

  private:
  void allocate_bucket_region();
  friend struct thread_data;
  public:
  void update_checksum(){checksum_post_sieve.update(bucket_region, BUCKET_REGION);}
};

struct thread_data : private NonCopyable {
  const thread_workspaces *ws;  /* a pointer to the parent structure, really */
  int id;
  thread_side_data sides[2];
  las_info const * plas;
  sieve_info * psi;
  las_report rep;       /* XXX obsolete, will be removed once the
                           timetree_t things absorbs everything. We still
                           need to decide what we do with the non-timer
                           (a.k.a. counter) data. */
  /* SS is used only in process_bucket region */
  unsigned char *SS;
  bool is_initialized;
  uint32_t first_region0_index;
  thread_data();
  ~thread_data();
  void init(const thread_workspaces &_ws, int id, las_info const & las);
  void pickup_si(sieve_info& si);
  void update_checksums();
};

struct thread_data_task_wrapper : public task_parameters {
    thread_data * th;
    void * (*f)(timetree_t&, thread_data *);
};

/* A set of n bucket arrays, all of the same type, and methods to reserve one
   of them for exclusive use and to release it again. */
template <typename T>
class reservation_array : private monitor {
    /* typically, T is here bucket_array<LEVEL, HINT>. It's a
     * non-copy-able object. Yet, it's legit to use std::vectors's on
     * such objects in c++11, provided that we limit ourselves to the
     * right constructor, and compiled code never uses allocation
     * changing modifiers.
     */
    std::vector<T> BAs;
    std::vector<bool> in_use;
  condition_variable cv;
  /* Return the index of the first entry that's currently not in use, or the
     first index out of array bounds if all are in use */
  size_t find_free() const {
    return std::find(in_use.begin(), in_use.end(), false) - in_use.begin();
  }
  reservation_array(reservation_array const &) = delete;
  reservation_array& operator=(reservation_array const&) = delete;
public:
  typedef typename T::update_t update_t;
  reservation_array(reservation_array &&) = default;
  reservation_array(size_t n) : BAs(n), in_use(n, false) { }

  /* Allocate enough memory to be able to store at least n_bucket buckets,
     each of size at least fill_ratio * bucket region size. */
  void allocate_buckets(const uint32_t n_bucket, double fill_ratio, int logI);
  // typename std::vector<T>::const_iterator cbegin() const {return BAs.cbegin();}
  // typename std::vector<T>::const_iterator cend() const {return BAs.cend();}
  // std::vector<T>& arrays() { return BAs; }
  std::vector<T> const& bucket_arrays() const { return BAs; }
  inline int rank(T const & BA) const { return &BA - &BAs.front(); }

  void reset_all_pointers() { for(auto & A : BAs) A.reset_pointers(); }

  T &reserve(int);
  void release(T &BA);
};

/* A group of reservation arrays, one for each possible update type.
   Also defines a getter function, templated by the desired type of
   update, that returns the corresponding reservation array, i.e.,
   it provides a type -> object mapping. */
class reservation_group {
  friend class thread_workspaces;
  reservation_array<bucket_array_t<1, shorthint_t> > RA1_short;
  reservation_array<bucket_array_t<2, shorthint_t> > RA2_short;
  reservation_array<bucket_array_t<3, shorthint_t> > RA3_short;
  reservation_array<bucket_array_t<1, longhint_t> > RA1_long;
  reservation_array<bucket_array_t<2, longhint_t> > RA2_long;
protected:
  template<int LEVEL, typename HINT>
  reservation_array<bucket_array_t<LEVEL, HINT> > &
  get();

  template <int LEVEL, typename HINT>
  const reservation_array<bucket_array_t<LEVEL, HINT> > &
  cget() const;
public:
  reservation_group(int nr_bucket_arrays, int nb_downsort_threads);
  void allocate_buckets(const uint32_t *n_bucket,
          bkmult_specifier const& multiplier,
          std::array<double, FB_MAX_PARTS> const &
          fill_ratio, int logI);
};

/* We have here nb_threads objects of type thread_data in the thrs[]
 * object, and nb_threads+1 (or 1 if nb_threads==1 anyway)
 * reservation_arrays in each data member of the two reservation_groups
 * in the groups[] data member. This +1 is here to allow work to spread
 * somewhat more evenly.
 */
class thread_workspaces : private NonCopyable {
  const int nb_threads;
  const int nr_workspaces;
  sieve_info * psi;
  static const unsigned int nr_sides = 2;
  reservation_group groups[2]; /* one per side */

public:
  // FIXME: thrs should be private!
  thread_data *thrs;

  thread_workspaces(int nb_threads, int nr_sides, las_info& _las);
  ~thread_workspaces();
  void pickup_si(sieve_info& si);
  void thread_do_using_pool(thread_pool&, void * (*) (timetree_t&, thread_data *));
  // void thread_do(void * (*) (thread_data *));
  void buckets_alloc();
  void buckets_free();

private:
  template <int LEVEL, typename HINT>
  double buckets_max_full();

public:

  double check_buckets_max_full();

  template <typename HINT>
  double check_buckets_max_full(int level, HINT const & hint);

  void accumulate_and_clear(las_report_ptr, sieve_checksum *);

  template <int LEVEL, typename HINT>
  void reset_all_pointers(int side);

  template <int LEVEL, typename HINT>
  bucket_array_t<LEVEL, HINT> &
  reserve_BA(const int side, int wish) {
      return groups[side].get<LEVEL, HINT>().reserve(wish);
  }

  template <int LEVEL, typename HINT>
  int rank_BA(const int side, bucket_array_t<LEVEL, HINT> const & BA) {
      return groups[side].get<LEVEL, HINT>().rank(BA);
  }

  template <int LEVEL, typename HINT>
  void
  release_BA(const int side, bucket_array_t<LEVEL, HINT> &BA) {
    return groups[side].get<LEVEL, HINT>().release(BA);
  }

  /*
   * not even needed. Better to expose only reserve() and release()
  template <int LEVEL, typename HINT>
  std::vector<bucket_array_t<LEVEL, HINT>> &
  bucket_arrays(int side) {return groups[side].get<LEVEL, HINT>().bucket_arrays();}
  */

  template <int LEVEL, typename HINT>
  std::vector<bucket_array_t<LEVEL, HINT>> const &
  bucket_arrays(int side) const {return groups[side].cget<LEVEL, HINT>().bucket_arrays();}

};

#endif
