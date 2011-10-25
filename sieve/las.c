#include "cado.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h> /* for PRIx64 macro and strtoumax */
#include <math.h>   // for ceiling, floor in cfrac
#include "fb.h"
#include "utils.h"           /* lots of stuff */
#include "basicnt.h"         /* ctzl bin_gcd */
#include "ecm/facul.h"
#include "bucket.h"
#include "trialdiv.h"
#include <pthread.h>
#include "mpz_poly.h"
#include "las-config.h"
#include "las-types.h"
#include "las-coordinates.h"
#include "las-debug.h"
#include "las-report-stats.h"
#include "las-norms.h"
#include "las-unsieve.h"
#include "las-arith.h"
#include "las-qlattice.h"

#define LOG_SCALE 1.4426950408889634 /* 1/log(2) to 17 digits, rounded to
                                        nearest. This is enough to uniquely
                                        identify the corresponding IEEE 754
                                        double precision number */

#ifndef HAVE_LOG2
static double MAYBE_UNUSED log2 (double x)
{
  return log (x) / log (2.0);
}
#endif

#ifndef HAVE_EXP2
static double MAYBE_UNUSED exp2 (double x)
{
  return exp (x * log (2.0));
}
#endif

/* This global mutex should be locked in multithreaded parts when a
 * thread does a read / write, especially on stdout, stderr...
 */
pthread_mutex_t io_mutex = PTHREAD_MUTEX_INITIALIZER; 

const int bucket_region = 1 << LOG_BUCKET_REGION;

/* for cofactorization statistics */
int stats = 0; /* 0: nothing, 1: write stats file, 2: read stats file,
                  the stats file can be used with gnuplot, for example:
                  splot "stats.dat" u 1:2:3, "stats.dat" u 1:2:4 */
double stats_prob = 2e-4;
FILE *stats_file;
FILE *sievestats_file;
uint32_t **cof_call; /* cof_call[r][a] is the number of calls of the
                        cofactorization routine with a cofactor of r bits on
                        the rational side, and a bits on the algebraic side */
uint32_t **cof_succ; /* cof_succ[r][a] is the corresponding number of
                        successes, i.e., of call that lead to a relation */


/* Test if entry x in bucket region n is divisible by p */
void test_divisible_x (const fbprime_t p, const unsigned long x, const int n,
		       sieve_info_srcptr si, int side);
int factor_leftover_norm (mpz_t n, unsigned int b, mpz_array_t* const factors,
			  uint32_array_t* const multis,
			  facul_strategy_t *strategy);

/* Determine whether a sieve entry with sieve residue S1 on sieving side 1
   and sieve residue S2 on sieving side 2 is likely smooth. 
   The array entry C1[S1] is initialized by sieve_info_init_lognorm() 
   to something similar to 
   -log(Pr[norm on side 1 with sieve residue S1 is smooth]),
   similar for C2, S2. Assuming the two probabilities are independent enough,
   we can estimate the neg log of the probability that both sides are smooth 
   by C1[S1] + C2[S2]. 
   If that sum does not exceed a theshold, the corresponding sieve entry is 
   a sieve survivor. 
   Alternative: have a bit array telling whether (S1,S2) is likely smooth */
static inline int 
sieve_info_test_lognorm (const unsigned char *C1, 
                         const unsigned char *C2, 
                         const unsigned char S1,
                         const unsigned char S2,
                         const unsigned char threshold)
{
  return C1[S1] + C2[S2] <= threshold;
}

static void sieve_info_init_trialdiv(sieve_info_ptr si)
{
    /* Our trial division needs odd divisors, 2 is handled by mpz_even_p().
       If the FB primes to trial divide contain 2, we skip over it.
       We assume that if 2 is in the list, it is the first list entry,
       and that it appears at most once. */
    for(int side = 0 ; side < 2 ; side++) {
        sieve_side_info_ptr s = si->sides[side];
        s->trialdiv_primes = fb_extract_bycost (s->fb, si->bucket_thresh,
                si->td_thresh);
        int n;
        for (n = 0; s->trialdiv_primes[n] != FB_END; n++);
        int skip2 = n > 0 && s->trialdiv_primes[0] == 2;
        s->trialdiv_data = trialdiv_init (s->trialdiv_primes + skip2,
                n - skip2);
    }
}

static void sieve_info_clear_trialdiv(sieve_info_ptr si)
{
    for(int side = 0 ; side < 2 ; side++) {
        trialdiv_clear (si->sides[side]->trialdiv_data);
        free (si->sides[side]->trialdiv_primes);
    }
}

static void sieve_info_init(sieve_info_ptr si, param_list pl)
{
    memset(si, 0, sizeof(sieve_info));

    si->outputname = param_list_lookup_string(pl, "out");
    /* Init output file */
    si->output = stdout;
    if (si->outputname) {
	if (!(si->output = gzip_open(si->outputname, "w"))) {
	    fprintf(stderr, "Could not open %s for writing\n", si->outputname);
	    exit(EXIT_FAILURE);
	}
    }

    param_list_print_command_line(si->output, pl);
    las_display_config_flags(si->output);

    si->verbose = param_list_parse_knob(pl, "-v");
    si->ratq = param_list_parse_knob(pl, "-ratq");
    si->nb_threads = 1;		/* default value */
    param_list_parse_int(pl, "mt", &si->nb_threads);
    if (si->nb_threads <= 0) {
	fprintf(stderr,
		"Error, please provide a positive number of threads\n");
	exit(EXIT_FAILURE);
    }

    cado_poly_init(si->cpoly);
    const char *tmp;
    if ((tmp = param_list_lookup_string(pl, "poly")) != NULL) {
	param_list_read_file(pl, tmp);
    }

    if (!cado_poly_set_plist(si->cpoly, pl)) {
	fprintf(stderr, "Error reading polynomial file\n");
	exit(EXIT_FAILURE);
    }

    /* -skew (or -S) may override (or set) the skewness given in the
     * polynomial file */
    param_list_parse_double(pl, "skew", &(si->cpoly->skew));

    if (si->cpoly->skew <= 0.0) {
	fprintf(stderr, "Error, please provide a positive skewness\n");
	exit(EXIT_FAILURE);
    }

    param_list_parse_int(pl, "I", &si->logI);
    si->I = 1 << si->logI;
    si->J = 1 << (si->logI - 1);


    fprintf(si->output,
	    "# Sieving parameters: rlim=%lu alim=%lu lpbr=%d lpba=%d\n",
	    si->cpoly->rat->lim, si->cpoly->alg->lim, si->cpoly->rat->lpb,
	    si->cpoly->alg->lpb);
    fprintf(si->output,
	    "#                     rat->mfb=%d alg->mfb=%d rlambda=%1.1f alambda=%1.1f\n",
	    si->cpoly->rat->mfb, si->cpoly->alg->mfb, si->cpoly->rat->lambda,
	    si->cpoly->alg->lambda);
    fprintf(si->output, "#                     skewness=%1.1f\n",
	    si->cpoly->skew);

    si->bucket_thresh = si->I;	/* default value */
    /* overrides default only if parameter is given */
    param_list_parse_int(pl, "bkthresh", &(si->bucket_thresh));
    si->td_thresh = 1024;	/* default value */
    param_list_parse_uint(pl, "tdthresh", &(si->td_thresh));

    /* Initialize the number of buckets */

    /* If LOG_BUCKET_REGION == (si->logI-1), then one bucket (whose size is the
     * L1 cache size) is actually one line. This changes some assumptions
     * in sieve_small_bucket_region and resieve_small_bucket_region, where
     * we want to differentiate on the parity on j.
     */
    ASSERT_ALWAYS(LOG_BUCKET_REGION >= (si->logI - 1));

#ifndef SUPPORT_I17
    if (si->logI >= 17) {
        fprintf(stderr,
                "Error: -I 17 requires setting the SUPPORT_I17 flag at compile time\n");
        abort();
    }
#endif

    si->nb_buckets = 1 + ((si->I / 2) * (si->J / 2) - 1) / bucket_region;
    si->bucket_limit_multiplier = BUCKET_LIMIT_FACTOR;
    fprintf(si->output, "# bucket_region = %u\n", bucket_region);
    fprintf(si->output, "# nb_buckets = %u\n", si->nb_buckets);

    sieve_info_init_unsieve_data(si);
}

/* Finds prime factors p < lim of n and returns a pointer to a zero-terminated
   list of those factors. Repeated factors are stored only once. */
static fbprime_t *
factor_small (mpz_t n, fbprime_t lim)
{
  unsigned long p;
  unsigned long l; /* number of prime factors */
  fbprime_t *f;

  l = 0;
  f = (fbprime_t*) malloc (sizeof (fbprime_t));
  FATAL_ERROR_CHECK(f == NULL, "malloc failed");
  for (p = 2; p <= lim; p = getprime (p))
    {
      if (mpz_divisible_ui_p (n, p))
        {
          l ++;
          f = (fbprime_t*) realloc (f, (l + 1) * sizeof (fbprime_t));
          FATAL_ERROR_CHECK(f == NULL, "realloc failed");
          f[l - 1] = p;
        }
    }
  f[l] = 0; /* end of list marker */
  getprime (0);
  return f;
}

static void
sieve_info_update (sieve_info_ptr si)
{
  if (si->verbose)
    fprintf (si->output, "# I=%u; J=%u\n", si->I, si->J);

  /* update number of buckets */
  
  si->nb_buckets = 1 + (si->I * si->J - 1) / bucket_region;
  
  /* essentially update the fij polynomials */
  sieve_info_update_norm_data(si);
}

static void
sieve_info_clear (sieve_info_ptr si)
{
  if (si->outputname)
      gzip_close(si->output, si->outputname);
  sieve_info_clear_unsieve_data(si);
  cado_poly_clear(si->cpoly);
}

// Compute the root r describing the lattice inside the q-lattice
// corresponding to the factor base prime (p,R).
// Formula: r = - (a1-R*b1)/(a0-R*b0) mod p
// Assumes p < 2^32

/* General version of the lattice transform function. Allows projective
   roots in input and output, and handles prime powers.
   In input, if the root is projective, say s/t (mod p) and t is
   non-invertible (mod p), then we expect R = p + (t/s mod p).
   On output, if the root is projective, say u/v (mod p) and v is
   non-invertible (mod p), then return value r = p + (v/u mod p).
   So projective roots are stored as their reciprocal, and have p added
   to signal the fact that it's a reciprocal value.
*/


/*
 * Algorithm by Franke and Kleinjung for lattice sieving of largish
 * primes.
 */

typedef struct {
    int32_t a0,b0;
    uint32_t a1,b1;
} plattice_info_t;

// Proposition 1 of [FrKl05]:
// Compute a basis <(alpha, beta), (gamma, delta)> of the p-lattice
// inside the q-lattice, such that
//    beta, delta > 0
//    -I < alpha <= 0 <= gamma < I
//    gamma-alpha >= I
//
// Sizes:
//    p is less than 32 bits and I fits easily in 32 bits.
//    So, alpha and beta fit easily in 32 bits, since they are less than I
//    Now, gamma and delta are also bounded by p, so 32 bits is enough
//    However: a and c can be as large as p*I (not both ?).
//    We still store them in 32 bits, since if they are larger, it means
//    that as soon as they are added to the offset for S, the index will
//    be out of range for S and the loop stops. Hence, this is safe to
//    replace a and c by a large value within 32 bits, when they are
//    larger than 32 bits.
//    Except that the choice of this ``large value'' requires some
//    caution. We need a value which can be used either for a or c, or
//    both, so that adding a, or c, or both, to a value within [0,IJ[ is
//    guaranteed to exceed IJ, but can't wrap around. Up to I=15, it's
//    rather easy. With the rescaling of J, at worst we may have IJ
//    within the range [2^29,2^30[. Thus if a and c are set to 2^30-1,
//    a.k.a INT32_MAX/2, then adding either, or the sum of both, to a
//    valid value x is guaranteed to be at most 3*2^30, which fits within
//    32 bits.
//    For I=16, it's much, much harder. Unless somebody comes up with a
//    nice idea, I see no way to avoid 64-bit arithmetic (which has some
//    cost, sure, but not terribly expensive). For consistency, we make
//    all data types for x, a, and c 64-bit in this case, and choose the
//    overflow constants as UINT32_MAX.
//

#ifdef SUPPORT_I16
typedef uint64_t plattice_x_t;
#else
typedef uint32_t plattice_x_t;
#endif

// Return value:
// * non-zero if everything worked ok
// * zero when the algorithm failed. This can happen when p is a prime power,
//   and g, gcd(p,r) >= I, since then the subtractive Euclidean algorithm will
//   yield (a0=g, b0=0) at some point --- or the converse --- and the loop
//   while (|a0| >= I) a0 += b0 will loop forever.
//
// Note that on a c166 example, this code alone accounts for almost 20%
// of the computation time.

NOPROFILE_INLINE int
reduce_plattice(plattice_info_t *pli, const fbprime_t p, const fbprime_t r, sieve_info_srcptr si)
{
    int32_t I = si->I;
    int32_t a0=-(int32_t)p, a1=0, b0=r, b1=1;
    int32_t hI = I;
#if MOD2_CLASSES_BS
    hI/=2;
#endif
    /* subtractive variant of Euclid's algorithm */
    for(;;) {
        /* a0 < 0 <= b0 < -a0 */
        if (b0 < hI) break;
        /* a0 < 0 < b0 < -a0 */
        for( ; a0 += b0, a1 += b1, a0 + b0 <= 0 ; );
        /* -b0 < a0 <= 0 < b0 */
        if (-a0 < hI) break;
        /* -b0 < a0 < 0 < b0 */
        for( ; b0 += a0, b1 += a1, b0 + a0 >= 0 ; );
        /* a0 < 0 <= b0 < -a0 */
    }
    if (b0 > -a0) {
        if (UNLIKELY(a0 == 0)) return 0;
        /* Now that |a0| < hI, we switch to classical division, since
           if say |a0|=1 and b0 is large, the subtractive variant
           will be very expensive.
           We want b0 + k*a0 < hI, i.e., b0 - hI + 1 <= k*(-a0),
           i.e., k = ceil((b0-hI+1)/a0). */
        int32_t k = 1 + (b0 - hI) / (-a0);
        b0 += k * a0;
        b1 += k * a1;
    } else {
        if (UNLIKELY(b0 == 0)) return 0;
        /* we switch to the classical algorithm here too */
        int32_t k = 1 + (-a0 - hI) / b0;
        a0 += k * b0;
        a1 += k * b1;
    }
    ASSERT (a1 > 0);
    ASSERT (b1 > 0);
    ASSERT ((a0 <= 0) && (a0 > -hI));
    ASSERT ((b0 >= 0) && (b0 <  hI));
    ASSERT (b0 - a0 >= hI);

    pli->a0 = a0;
    pli->a1 = a1;
    pli->b0 = b0;
    pli->b1 = b1;

#if 0
    int32_t J = si->J;
#if MOD2_CLASSES_BS
#if 1
#endif

#if 1 || !MOD2_CLASSES
    // left-shift everybody, since the following correspond to the
    // lattice 2p.
    a0 <<= 1; a1 <<= 1;
    b0 <<= 1; b1 <<= 1;
#endif
#endif

    pli->a = ((a1 << si->logI) + a0);
    pli->c = ((b1 << si->logI) + b0);
    if (a1 > J || (a1 == J && a0 > 0)) { pli->a = INT32_MAX/2; }
    if (b1 > J || (b1 == J && b0 > 0)) { pli->c = INT32_MAX/2; }

    /* It's difficult to encode everybody in 32 bits, and still keep
     * relevant information...
     */
    pli->bound0 = -a0;
    pli->bound1 = I - b0;
#endif
    return 1;
}

#if MOD2_CLASSES_BS
#define PLI_COEFF(pli, ab01) (pli->ab01 << 1)
#else
#define PLI_COEFF(pli, ab01) (pli->ab01)
#endif
static inline plattice_x_t plattice_a(const plattice_info_t * pli, sieve_info_srcptr si)
{
    int32_t a0 = PLI_COEFF(pli, a0);
    uint32_t a1 = PLI_COEFF(pli, a1);
    if (a1 > (uint32_t) si->J || (a1 == (uint32_t) si->J && a0 > 0))
#ifdef SUPPORT_I16
        return UINT32_MAX;
#else
        return INT32_MAX/2;
#endif
    else
        return (a1 << si->logI) + a0;
}

static inline plattice_x_t plattice_c(const plattice_info_t * pli, sieve_info_srcptr si)
{
    int32_t b0 = PLI_COEFF(pli, b0);
    uint32_t b1 = PLI_COEFF(pli, b1);
    if (b1 > (uint32_t) si->J || (b1 == (uint32_t) si->J && b0 > 0))
#ifdef SUPPORT_I16
        return UINT32_MAX;
#else
        return INT32_MAX/2;
#endif
    else
        return (b1 << si->logI) + b0;
}

static inline uint32_t plattice_bound0(const plattice_info_t * pli, sieve_info_srcptr si MAYBE_UNUSED)
{
    return - PLI_COEFF(pli, a0);
}

static inline uint32_t plattice_bound1(const plattice_info_t * pli, sieve_info_srcptr si)
{
    return si->I - PLI_COEFF(pli, b0);
}


/* This is for working with congruence classes only */
NOPROFILE_INLINE
plattice_x_t plattice_starting_vector(const plattice_info_t * pli, sieve_info_srcptr si, int par MAYBE_UNUSED)
{
    /* With MOD2_CLASSES_BS set up, we have computed by the function
     * above an adapted basis for the band of total width I/2 (thus from
     * -I/4 to I/4). This adapted basis is in the coefficients a0 a1 b0
     *  b1 of the pli data structure.
     *
     * Now as per Proposition 1 of FrKl05 applied to I/2, any vector
     * whose i-coordinates are within ]-I/2,I/2[ (<ugly>We would like a
     * closed interval on the left. Read further on for that case</ugly>)
     * can actually be written as a combination with positive integer
     * coefficients of these basis vectors a and b.
     *
     * We also know that the basis (a,b) has determinant p, thus odd. The
     * congruence class mod 2 that we want to reach is thus accessible.
     * It is even possible to find coefficients (k,l) in {0,1} such that
     * ka+lb is within this congruence class. This means that we're going
     * to consider either a,b,or a+b as a starting vector. The
     * i-coordinates of these, as per the properties of Proposition 1, are
     * within ]-I/2,I/2[. Now all other vectors with i-coordinates in
     * ]-I/2,I/2[ which also belong to the same congruence class, can be
     * written as (2k'+k)a+(2l'+l)b, with k' and l' necessarily
     * nonnegative.
     *
     * The last ingredient is that (2a, 2b) forms an adapted basis for
     * the band of width I with respect to the lattice 2p. It's just an
     * homothety.
     *
     * To find (k,l), we proceed like this. First look at the (a,b)
     * matrix mod 2:
     *                 a0&1    a1&1
     *                 b0&1    b1&1
     * Its determinant is odd, thus the inverse mod 2 is:
     *                 b1&1    a1&1
     *                 b0&1    a0&1
     * Now the congruence class is given by the parity argument. The
     * vector is:
     *                par&1,   par>>1
     * Multiplying this vector by the inverse matrix above, we obtain the
     * coordinates k,l, which are:
     *            k = (b1&par&1)^(b0&(par>>1));
     *            l = (a1&par&1)^(a0&(par>>1));
     * Now our starting vector is ka+lb. Instead of multiplying by k and
     * l with values in {0,1}, we mask with -k and -l, which both are
     * either all zeroes or all ones in binary
     *
     */
    /* Now for the extra nightmare. Above, we do not have the guarantee
     * that a vector whose i-coordinate is precisely -I/2 has positive
     * coefficients in our favorite basis. It's annoying, because it may
     * well be that if such a vector also has positive j-coordinate, then
     * it is in fact the first vector we will meet. An example is given
     * by the following data:
     *
            f:=Polynomial(StringToIntegerSequence("
                -1286837891385482936880099433527136908899552
                55685111236629140623629786639929578
                13214494134209131997428776417
                -319664171270205889372
                -17633182261156
                40500"));

            q:=165017009; rho:=112690811;
            a0:=52326198; b0:=-1; a1:=60364613; b1:=2;
            lI:=13; I:=8192; J:=5088;
            p:=75583; r0:=54375;
            > M;
            [-2241    19]
            [ 1855    18]
            > M[1]-M[2];
            (-4096     1)

    * Clearly, above, for the congruence class (0,1), we must start with
    * this vector, not with the sum.
    */
#if !MOD2_CLASSES_BS
    /* In case we don't consider congruence classes at all, then there is
     * nothing very particular to be done */
    plattice_x_t x = (1 << (si->logI-1));
    uint32_t i = x;
    if (i >= plattice_bound1(pli, si)) x += plattice_a(pli, si);
    if (i <  plattice_bound0(pli, si)) x += plattice_c(pli, si);
    return x;
#else
    int32_t a0 = pli->a0;
    int32_t a1 = pli->a1;
    int32_t b0 = pli->b0;
    int32_t b1 = pli->b1;

    int k = -((b1&par&1)^(b0&(par>>1)));
    int l = -((a1&par&1)^(a0&(par>>1)));
    int32_t v[2]= { (a0&k)+(b0&l), (a1&k)+(b1&l)};

    /* handle exceptional case as described above */
    if (k && l && a0-b0 == -(1 << (si->logI-1)) && a1 > b1) {
        v[0] = a0-b0;
        v[1] = a1-b1;
    }

    if (v[1] > (int32_t) si->J)
#ifdef SUPPORT_I16
        return UINT32_MAX;
#else
        return INT32_MAX/2;
#endif
    return (v[1] << si->logI) | (v[0] + (1 << (si->logI-1)));
#endif
}

/***************************************************************************/
/* {{{ Structures for small sieves */

/* TODO: move the definition of these small_* structures closer to the
 * code for small sieves, to the extent possible */

typedef struct {
    fbprime_t p;
    fbprime_t r;        // in [ 0, p [
    fbprime_t offset;   // in [ 0, p [
} ssp_t;

/* We currently *mandate* that this structure has the same size as ssp_t.
 * It would be possible to make it work with only a requirement on
 * identical alignment.
 */
typedef struct {
    fbprime_t g, q, U;
} ssp_bad_t;

#define SSP_POW2        (1u<<0)
#define SSP_PROJ        (1u<<1)
#define SSP_DISCARD     (1u<<30)
#define SSP_END         (1u<<31)

typedef struct {
    int index;
    unsigned int event;
} ssp_marker_t;

typedef struct {
    ssp_marker_t * markers;
    // primes with non-projective root
    ssp_t *ssp;
    // primes with projective root
    int nb_ssp;
    unsigned char * logp;
    int * next_position;
} small_sieve_data_t;

#define PUSH_SSP_MARKER(ssd, nmarkers, __index, __event) do {		\
    ssd->markers = (ssp_marker_t *) realloc(ssd->markers,               \
            (nmarkers + 1) * sizeof(ssp_marker_t));                     \
    ssd->markers[nmarkers].index = __index;				\
    ssd->markers[nmarkers].event = __event;				\
    nmarkers++;								\
} while (0)


/* }}} */

/***************************************************************************/
/********        Main bucket sieving functions                    **********/

/* All of this exists _for each thread_ */
struct thread_side_data_s {
    bucket_array_t BA;
    factorbase_degn_t *fb_bucket; /* in reality a pointer into a shared array */
    double bucket_fill_ratio;     /* inverse sum of bucket-sieved primes */
};
typedef struct thread_side_data_s thread_side_data[1];
typedef struct thread_side_data_s * thread_side_data_ptr;
typedef const struct thread_side_data_s * thread_side_data_srcptr;

struct thread_data_s {
    int id;
    thread_side_data sides[2];
    sieve_info_ptr si;
    las_report rep;
};
typedef struct thread_data_s thread_data[1];
typedef struct thread_data_s * thread_data_ptr;
typedef const struct thread_data_s * thread_data_srcptr;

/* {{{ dispatch_fb */
static void dispatch_fb(factorbase_degn_t ** fb_dst, factorbase_degn_t ** fb_main, factorbase_degn_t * fb0, int nparts, fbprime_t pmax)
{
    /* Given fb0, which is a pointer in the fb array * fb_main, allocates
     * fb_dst[0] up to fb_dst[nparts-1] as independent fb arrays, each of
     * appropriate length to contain equivalent portions of the _tail_ of
     * the fb array fb_main, starting at pointer fb0. Reallocates *fb_main
     * in the end.
     */
    /* Start by counting, unsurprisingly */
    size_t * fb_sizes = (size_t *) malloc(nparts * sizeof(size_t));
    FATAL_ERROR_CHECK(fb_sizes == NULL, "malloc failed");
    memset(fb_sizes, 0, nparts * sizeof(size_t));
    size_t headsize = fb_diff_bytes(fb0, *fb_main);
    int i = 0;
    for(factorbase_degn_t * fb = fb0 ; fb->p != FB_END && fb->p <= pmax; fb = fb_next (fb)) {
        size_t sz = fb_entrysize (fb); 
        fb_sizes[i] += sz;
        i++;
        i %= nparts;
    }
    factorbase_degn_t ** fbi = (factorbase_degn_t **) malloc(nparts * sizeof(factorbase_degn_t *));
    for(i = 0 ; i < nparts ; i++) {
        // add one for end marker
        fb_sizes[i] += sizeof(factorbase_degn_t);
        fb_dst[i] = (factorbase_degn_t *) malloc(fb_sizes[i]);
        FATAL_ERROR_CHECK(fb_dst[i] == NULL, "malloc failed");
        fbi[i] = fb_dst[i];
    }
    free(fb_sizes); fb_sizes = NULL;
    i = 0;
    int k = 0;
    for(factorbase_degn_t * fb = fb0 ; fb->p != FB_END && fb->p <= pmax; fb = fb_next (fb)) {
        k++;
        size_t sz = fb_entrysize (fb); 
        memcpy(fbi[i], fb, sz);
        fbi[i] = fb_next(fbi[i]);
        i++;
        i %= nparts;
    }
    for(i = 0 ; i < nparts ; i++) {
        memset(fbi[i], 0, sizeof(factorbase_degn_t));
        fbi[i]->p = FB_END;
    }
    free(fbi); fbi = NULL;
    *fb_main = realloc(*fb_main, (headsize + sizeof(factorbase_degn_t)));
    FATAL_ERROR_CHECK(*fb_main == NULL, "realloc failed");
    fb0 = fb_skip(*fb_main, headsize);
    memset(fb0, 0, sizeof(factorbase_degn_t));
    fb0->p = FB_END;
}
/* }}} */

/* {{{ fill_in_buckets */
void
fill_in_buckets(thread_data_ptr th, int side, where_am_I_ptr w MAYBE_UNUSED)
{
    WHERE_AM_I_UPDATE(w, side, side);
    sieve_info_srcptr si = th->si;
    bucket_array_t BA = th->sides[side]->BA;  /* local copy */
    // Loop over all primes in the factor base.
    //
    // Note that dispatch_fb already arranged so that all the primes
    // which appear here are >= bucket_thresh and <= pmax (the latter
    // being for the moment unconditionally set to FBPRIME_MAX by the
    // caller of dispatch_fb).

    fb_iterator t;
    fb_iterator_init_set_fb(t, th->sides[side]->fb_bucket);
    for( ; !fb_iterator_over(t) ; fb_iterator_next(t)) {
        fbprime_t p = t->fb->p;
        unsigned char logp = t->fb->plog;
        ASSERT_ALWAYS (p % 2 == 1);

        WHERE_AM_I_UPDATE(w, p, p);
        /* Write new set of pointers if the logp value changed */
        bucket_new_logp (&BA, logp);

        /* If we sieve for special-q's smaller than the factor
           base bound, the prime p might equal the special-q prime q. */
        if (UNLIKELY(p == si->q))
            continue;

        const uint32_t I = si->I;
        const int logI = si->logI;
        const uint32_t even_mask = (1U << logI) | 1U;
        const uint32_t maskI = I-1;
        const uint32_t maskbucket = bucket_region - 1;
        const int shiftbucket = LOG_BUCKET_REGION;
        const uint32_t IJ = si->I * si->J;
        fbprime_t r, R;

        R = fb_iterator_get_r(t);
        r = fb_root_in_qlattice(p, R, t->fb->invp, si);
        // TODO: should be line sieved in the non-bucket phase?
        // Or should we have a bucket line siever?
        if (UNLIKELY(r == 0))
        {
            /* If r == 0 (mod p), this prime hits for i == 0 (mod p),
               but since p > I, this implies i = 0 or i > I. We don't
               sieve i > I. Since gcd(i,j) | gcd(a,b), for i = 0 we
               only need to sieve j = 1 */
            /* x = j*I + (i + I/2) = I + I/2 */
            bucket_update_t update;
            uint32_t x = I + I / 2;
            update.x = (uint16_t) (x & maskbucket);
            update.p = bucket_encode_prime (p);
            WHERE_AM_I_UPDATE(w, N, x >> shiftbucket);
            WHERE_AM_I_UPDATE(w, x, update.x);
            ASSERT(test_divisible(w));
            push_bucket_update(BA, x >> shiftbucket, update);
            continue;
        }
        if (UNLIKELY(r == p))
        {
            /* r == p means root at infinity, which hits for
               j == 0 (mod p). Since q > I > J, this implies j = 0
               or j > J. This means we sieve only (i,j) = (1,0) here.
               Since I < bucket_region, this always goes in bucket 0.
FIXME: what about (-1,0)? It's the same (a,b) as (1,0)
but which of these two (if any) do we sieve? */
            bucket_update_t update;
            update.x = (uint16_t) I / 2 + 1;
            update.p = bucket_encode_prime (p);
            WHERE_AM_I_UPDATE(w, N, 0);
            WHERE_AM_I_UPDATE(w, x, update.x);
            ASSERT(test_divisible(w));
            push_bucket_update(BA, 0, update);
            continue;
        }
        if (UNLIKELY(r > p))
        {
            continue;
        }

        /* If working with congruence classes, once the loop on the
         * parity goes at the level above, this initialization
         * should in fact either be done for each congruence class,
         * or saved for later use within the factor base structure.
         */
        plattice_info_t pli;
        if (reduce_plattice(&pli, p, r, si) == 0)
        {
            pthread_mutex_lock(&io_mutex);
            fprintf (stderr, "# fill_in_buckets: reduce_plattice() "
                    "returned 0 for p = " FBPRIME_FORMAT ", r = "
                    FBPRIME_FORMAT "\n", p, r);
            pthread_mutex_unlock(&io_mutex);
            continue; /* Simply don't consider that (p,r) for now.
FIXME: can we find the locations to sieve? */
        }

        uint32_t bound0 = plattice_bound0(&pli, si);
        uint32_t bound1 = plattice_bound1(&pli, si);

        for(int parity = MOD2_CLASSES_BS ; parity < (MOD2_CLASSES_BS?4:1) ; parity++) {

            // The sieving point (0,0) is I/2 in x-coordinate
            plattice_x_t x = plattice_starting_vector(&pli, si, parity);
            // TODO: check the generated assembly, in particular, the
            // push function should be reduced to a very simple step.
            bucket_update_t update;
            update.p = bucket_encode_prime (p);
            __asm__("## Inner bucket sieving loop starts here!!!\n");
            plattice_x_t inc_a = plattice_a(&pli, si);
            plattice_x_t inc_c = plattice_c(&pli, si);
            // ASSERT_ALWAYS(inc_a == pli.a);
            // ASSERT_ALWAYS(inc_c == pli.c);
            while (x < IJ) {
                uint32_t i;
                i = x & maskI;   // x mod I
                /* if both i = x % I and j = x / I are even, then
                   both a, b are even, thus we can't have a valid relation */
                /* i-coordinate = (x % I) - I/2
                   (I/2) % 3 == (-I) % 3, hence
                   3|i-coordinate iff (x%I+I) % 3 == 0 */
                if (MOD2_CLASSES_BS || (x & even_mask) 
#ifdef SKIP_GCD3
                        && (!is_divisible_3_u32 (i + I) ||
                            !is_divisible_3_u32 (x >> logI))
#endif
                   )
                {
#if LOG_BUCKET_REGION == 16 && defined(__x86_64__) && defined(__GNUC__)
                    /* The x value in update can be set by a write to 
                       the low word of the register, but gcc does not 
                       do so - it writes the word to memory, then reads 
                       the dword back again. */
                    __asm__ (
                            "movw %1, %w0\n\t"
                            : "+r" (update)
                            : "r" ((uint16_t) (x & maskbucket))
                            );
#else
                    update.x = (uint16_t) (x & maskbucket);
#endif
                    WHERE_AM_I_UPDATE(w, N, x >> shiftbucket);
                    WHERE_AM_I_UPDATE(w, x, update.x);
                    ASSERT(test_divisible(w));
#ifdef PROFILE
                    /* To make it visible in profiler */
                    *(BA.bucket_write[x >> shiftbucket])++ = update;
#else
                    push_bucket_update(BA, x >> shiftbucket, update);
#endif
                }
#ifdef TRACE_K
                if (trace_on_spot_x(x)) {
                    fprintf (stderr, "# Pushed (%u, %u) (%u, %s) to BA[%u]\n",
                            (unsigned int) (x & maskbucket), logp, p, sidenames[side], (unsigned int) (x >> shiftbucket));
                }
#endif
                if (i >= bound1) x += inc_a;
                if (i < bound0)  x += inc_c;
            }
            __asm__("## Inner bucket sieving loop stops here!!!\n");
        }
    }
    /* Write back BA so the nr_logp etc get copied to caller */
    th->sides[side]->BA = BA;
}

void * fill_in_buckets_both(thread_data_ptr th)
{
    where_am_I w;
    WHERE_AM_I_UPDATE(w, si, th->si);
    fill_in_buckets(th, ALGEBRAIC_SIDE, w);
    fill_in_buckets(th, RATIONAL_SIDE, w);
    return NULL;
}
/* }}} */

void thread_do(thread_data * thrs, void * (*f) (thread_data_ptr))
{
    sieve_info_ptr si = thrs[0]->si;
    if (si->nb_threads == 1) {
        /* Then don't bother with pthread calls */
        (*f)(thrs[0]);
        return;
    }
    pthread_t * th = malloc(si->nb_threads*sizeof(pthread_t)); 
    ASSERT_ALWAYS(th);

#if 0
    /* As a debug measure, it's possible to activate this branch instead
     * of the latter. In effect, this causes las to run in a
     * non-multithreaded way, albeit strictly following the code path of
     * the multithreaded case.
     */
    for (int i = 0; i < si->nb_threads; ++i) {
        (*f)(thrs[i]);
    }
#else
    for (int i = 0; i < si->nb_threads; ++i) {
        int ret = pthread_create(&(th[i]), NULL, 
		(void * (*)(void *)) f,
                (void *)(thrs[i]));
        ASSERT_ALWAYS(ret == 0);
    }
    for (int i = 0; i < si->nb_threads; ++i) {
        int ret = pthread_join(th[i], NULL);
        ASSERT_ALWAYS(ret == 0);
    }
#endif

    free(th);
}

/* {{{ apply_buckets */
NOPROFILE_STATIC void
apply_one_bucket (unsigned char *S, bucket_array_t BA, const int i,
        where_am_I_ptr w)
{
    int j = nb_of_updates(BA, i);
    int next_logp_j = 0;
    unsigned char logp = 0;
    bucket_update_t *next_logp_change, *read_ptr;

    /* Having the read_ptr here defeats the whole idea of having 
       nice inline functions to handle all the BA stuff, but I yet 
       need to figure out how to keep gcc from writing 
       BA.bucket_read[i] back to memory and reading it from memory again
       on every get_next_bucket_update()  */
    read_ptr = BA.bucket_read[i];

    /* Init so that first access fetches logp */
    next_logp_j = 0;
    next_logp_change = read_ptr;

    WHERE_AM_I_UPDATE(w, p, 0);

    for (; j > 0; --j) {
       uint16_t x;

       /* Do we need a new logp ? */
       if (read_ptr >= next_logp_change)
         {
           ASSERT_ALWAYS (next_logp_j < BA.nr_logp);
           ASSERT_ALWAYS (BA.logp_idx[next_logp_j * BA.n_bucket + i] 
                           == next_logp_change);
           logp = BA.logp_val[next_logp_j++];
           /* Get pointer telling when to fetch new logp next time */
           if (next_logp_j < BA.nr_logp)
             next_logp_change = BA.logp_idx[next_logp_j * BA.n_bucket + i];
           else
             next_logp_change = BA.bucket_write[i]; /* effectively: never */
         }
       
       x = (read_ptr++)->x;
       WHERE_AM_I_UPDATE(w, x, x);
       sieve_decrease (S + x, logp, w);
    }
}
/* }}} */

/* {{{ small sieve and resieving */
/* Small primes or powers of small primes p^k with projective root.
   These hit at 
     i*v == j*u (mod p^k) 
   for some u,v in Z, but gcd(v, p^k) > 1.
   We may assume gcd(u,p)==1, or we divide the entire equation by p.
   XXX [ET]: we should also assume that v is a prime power, and that u
   XXX [ET]: is within [0..p^k/v-1[ ; 
   We store g = gcd(v, p^k), q = p^k / g, and U = u * (v/g)^(-1) (mod q).
   XXX [ET]: which would then imply g==v, q=p^k/v, and U=u

   Then we have
     i*v == j*u (mod p^k)  <==>  i == (j/g)*U (mod q)
   with g|j. 
   
   In other words, we can sieve this bad prime (powers) much like a 
   normal prime (power) q with root U, except that after sieving a line 
   we don't advance by one line, but by g lines.
   The case where g = q^k and thus q = 1 can be sieved more efficiently,
   of course, since every entry in each g-th line will be hit, so that
   the sieving should use long word transfers.

   Just like for normal primes, the next_position value points at the first
   position to sieve relative to the start of the current sieve region.

   Within a line that starts at index line_start, for array element of 
   index x, we have x - line_start = i+I/2. 
   We skip j=0, as it contains only the single possible relation 
   (i,j) = (1,0). 
   For j=1*g, we want i=U (mod q), so x - line_start == I/2+U (mod q),
   so we initialise 
     next_position = I*g + (I/2 + U) % q
   to get the first array index in line j=g, 
   then within a line sieve next_position + t*q < I, t in N,
   and update 
     next_position = (next_position - line_start + U) % q + line_start + g*I 
   to get the first position to sieve in the next suitable line. */

/* FIXME: This next_position update above is similar to the offset field
 * for typical primes, except that we have these larger jumps...
 */

/* TODO: put back small_sieve_data_t structure once code movement is
 * finished.
 */
void small_sieve_clear(small_sieve_data_t * ssd) {
    free(ssd->ssp); ssd->ssp = NULL;
    free(ssd->logp); ssd->logp = NULL;
    free(ssd->next_position); ssd->next_position = NULL;
    free(ssd->markers); ssd->markers = NULL;
}

void
small_sieve_clone(small_sieve_data_t *r, const small_sieve_data_t *s) {
    memcpy(r, s, sizeof(small_sieve_data_t));
    r->next_position = malloc(r->nb_ssp*sizeof(int));
    FATAL_ERROR_CHECK(r->next_position == NULL, "malloc failed");
    memcpy(r->next_position, s->next_position, r->nb_ssp * sizeof(int));
}

void
small_sieve_clear_cloned(small_sieve_data_t *r) {
    free(r->next_position);
    memset(r, 0, sizeof(small_sieve_data_t));
}

static void ssd_print_contents(FILE * f, const char * prefix, small_sieve_data_t * ssd) /* {{{ */
{
    ssp_marker_t * next_marker = ssd->markers;
    int nice=ssd->nb_ssp;
    int nproj=0;
    int npow2=0;
    int ndiscard=0;
    for( ; next_marker->event != SSP_END ; next_marker++) {
        unsigned int event = next_marker->event;
        nproj += ((event & SSP_PROJ) != 0);
        npow2 += ((event & SSP_POW2) != 0);
        ndiscard += ((event & SSP_DISCARD) != 0);
        nice -= (nproj || npow2 || ndiscard) != 0;
    }
    ASSERT_ALWAYS(next_marker->index == ssd->nb_ssp);

    fprintf (f, "# %s: %d nice primes", prefix, nice);
    /* Primes may be both even and projective... */
    if (npow2) fprintf (f, ", %d powers of 2", npow2);
    if (nproj) fprintf (f, ", and %d projective primes", nproj);
    fprintf (f, ".");
    if (ndiscard) fprintf (f, " %d discarded.", ndiscard);
    fprintf (f, "\n");
} /* }}} */


static void ssd_info(sieve_info_srcptr si, const char * what, int side, small_sieve_data_t * r)
{
    if (!si->verbose) return;
    char * tmp;
    int rc = asprintf(&tmp, "%s(%s side)", what, sidenames[side]);
    ASSERT_ALWAYS(rc >= 0);
    ssd_print_contents(si->output, tmp, r);
    free(tmp);
}

/* Copy those primes in s to r that need to be resieved, i.e., those
   that are not in trialdiv_primes and that are not prime powers */
    static void
init_resieve (small_sieve_data_t *r, const small_sieve_data_t *s, 
        const fbprime_t *trialdiv_primes)
{
    const fbprime_t * td = trialdiv_primes;

    int j = 0;
    r->ssp = (ssp_t *) malloc (s->nb_ssp * sizeof (ssp_t));
    FATAL_ERROR_CHECK(r->ssp == NULL, "malloc failed");
    r->logp = malloc (s->nb_ssp);
    FATAL_ERROR_CHECK(r->logp == NULL, "malloc failed");
    r->next_position = (int *) malloc (s->nb_ssp * sizeof(int));
    FATAL_ERROR_CHECK(r->next_position == NULL, "malloc failed");
    r->markers = NULL;
    int r_nmarkers = 0;

    ssp_marker_t * next_marker = s->markers;

    for(int i = 0 ; i < s->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        do {
            event = next_marker->event;
            fence = next_marker->index;
            next_marker++;
            /* Powers of two don't need any special treatment. Note also
             * that 2 is never resieved obviously, so we don't have to pass
             * the POW2 marker to the child ssd info struct */
        } while ((event & (~SSP_POW2)) == 0);
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(s->ssp[i]);
            if (is_prime_power(ssp->p)) continue;
            for( ; *td != FB_END && *td < ssp->p ; td++);
            if (*td == FB_END || *td != ssp->p) {
                r->ssp[j] = s->ssp[i];
                r->logp[j] = s->logp[i];
                r->next_position[j] = s->next_position[i];
                j++;
            }
        }
        if (event & SSP_END) {
            ASSERT_ALWAYS(i == s->nb_ssp);
            break;
        }
        /* Now prime number i in the list has something special */
        if (event & SSP_DISCARD) continue;
        /* We're restricted to the projective case here. So just convert
         * the ssp data, and redo the reasoning based on the bad prime
         * case.  */
        ASSERT_ALWAYS(event & SSP_PROJ);
        ssp_bad_t * ssp = (ssp_bad_t * ) &(s->ssp[i]);
        /* p^k = q*g, g > 1, so k>1 if g is power or if q > 1 */
        if (ssp->q > 1 || is_prime_power(ssp->g))
            continue;
        /* At this point q==1, so g==p */
        for( ; *td != FB_END && *td < ssp->g ; td++);
        /* Note that we may have ``holes'' in the trialdiv_primes list.
         * So we may jump from strictly below p to strictly above.
         */
        if (*td == FB_END || *td != ssp->g) {
            /* It's not a trial-div'ed prime, so we schedule it for
             * resieving */
            PUSH_SSP_MARKER(r, r_nmarkers, j, SSP_PROJ);
            r->ssp[j] = s->ssp[i];
            r->logp[j] = s->logp[i];
            r->next_position[j] = s->next_position[i];
            j++;
        }
    }
    r->nb_ssp = j;
    PUSH_SSP_MARKER(r, r_nmarkers, j, SSP_END);
}

/* We can plan on splitting the small factor base in several
 * non-overlapping, contiguous zones:
 *
 *      - powers of 2 (up until the pattern sieve limit)
 *      - powers of 3 (up until the pattern sieve limit)
 *      - trialdiv primes (not powers)
 *      - resieved primes
 *      (- powers of trialdiv primes)
 *      - rest.
 *
 * Problem: bad primes may in fact be pattern sieved, and we might want
 * to pattern-sieve more than just the ``do it always'' cases where p is
 * below the pattern sieve limit.
 *
 * The answer to this is that such primes are expected to be very very
 * rare, so we don't really bother. If we were to do something, we could
 * imagine setting up a schedule list for projective primes -- e.g. a
 * priority queue. But it feels way overkill.
 *
 * Note that the pre-treatment (splitting the factor base in chunks) can
 * be done once and for all.
 */

void reorder_fb(sieve_info_ptr si, int side)
{
    factorbase_degn_t * fb_pow2, * fb_pow2_base;
    factorbase_degn_t * fb_pow3, * fb_pow3_base;
    factorbase_degn_t * fb_td, * fb_td_base;
    // factorbase_degn_t * fb_pow_td, * fb_pow_td_base;
    factorbase_degn_t * fb_rs, * fb_rs_base;
    factorbase_degn_t * fb_rest, * fb_rest_base;

    factorbase_degn_t * fb_base = si->sides[side]->fb;
    factorbase_degn_t * fb = fb_base;

    size_t sz = fb_size(fb);

    fb_pow2 = fb_pow2_base = (factorbase_degn_t *) malloc(sz);
    fb_pow3 = fb_pow3_base = (factorbase_degn_t *) malloc(sz);
    fb_td = fb_td_base = (factorbase_degn_t *) malloc(sz);
    // fb_pow_td = fb_pow_td_base = (factorbase_degn_t *) malloc(sz);
    fb_rs = fb_rs_base = (factorbase_degn_t *) malloc(sz);
    fb_rest = fb_rest_base = (factorbase_degn_t *) malloc(sz);

    fbprime_t plim = si->bucket_thresh;
    fbprime_t costlim = si->td_thresh;

#define PUSH_LIST(x) do {						\
            memcpy(fb_## x, fb, fb_entrysize(fb));			\
            fb_## x = fb_next(fb_## x);					\
} while (0)

    size_t pattern2_size = sizeof(unsigned long) * 2;
    for( ; fb->p != FB_END ; fb = fb_next(fb)) {
        /* The extra conditions on powers of 2 and 3 are related to how
         * pattern-sieving is done.
         */
        if ((fb->p%2)==0 && fb->p <= pattern2_size) {
            PUSH_LIST(pow2);
        } else if (fb->p == 3) {
            PUSH_LIST(pow3);
        } else if (fb->p <= plim && fb->p <= costlim * fb->nr_roots) {
            if (!is_prime_power(fb->p)) {
                PUSH_LIST(td);
            } else {
                // PUSH_LIST(pow_td);
                PUSH_LIST(rest);
            }
        } else {
            if (!is_prime_power(fb->p)) {
                PUSH_LIST(rs);
            } else {
                PUSH_LIST(rest);
            }
        }
    }
#undef PUSH_LIST

#define APPEND_LIST(x) do {						\
    char * pb = (char*) (void*) fb_ ## x ## _base;			\
    char * p  = (char*) (void*) fb_ ## x;				\
    si->sides[side]->fb_parts->x[0] = fb;                               \
    si->sides[side]->fb_parts_x->x[0] = n;                              \
    memcpy(fb, pb, p - pb);						\
    fb = fb_skip(fb, p - pb);						\
    n += fb_diff(fb_ ## x, fb_ ## x ## _base);                          \
    si->sides[side]->fb_parts->x[1] = fb;                               \
    si->sides[side]->fb_parts_x->x[1] = n;                              \
} while (0)
    int n = 0;
    fb = fb_base;

    APPEND_LIST(pow2);
    APPEND_LIST(pow3);
    APPEND_LIST(td);
    APPEND_LIST(rs);
    APPEND_LIST(rest);
    fb->p = FB_END;

    free(fb_pow2_base);
    free(fb_pow3_base);
    free(fb_td_base);
    free(fb_rs_base);
    free(fb_rest_base);

#undef  APPEND_LIST

    if (si->verbose) {
        fprintf(si->output, "# small %s factor base", sidenames[side]);
        factorbase_degn_t ** q;
        q = si->sides[side]->fb_parts->pow2;
        fprintf(si->output, ": %d pow2", fb_diff(q[1], q[0]));
        q = si->sides[side]->fb_parts->pow3;
        fprintf(si->output, ", %d pow3", fb_diff(q[1], q[0]));
        q = si->sides[side]->fb_parts->td;
        fprintf(si->output, ", %d td", fb_diff(q[1], q[0]));
        q = si->sides[side]->fb_parts->rs;
        fprintf(si->output, ", %d rs", fb_diff(q[1], q[0]));
        q = si->sides[side]->fb_parts->rest;
        fprintf(si->output, ", %d rest", fb_diff(q[1], q[0]));
        fprintf(si->output, " (total %zu)\n", fb_nroots_total(fb_base));
    }
}

// Prepare sieving of small primes: initialize a small_sieve_data_t
// structure to be used thereafter during sieving each region.
// next_position points at the next position that will be hit by sieving,
// relative to the start of the next bucket region to sieve. It may exceed I 
// and even BUCKET_REGION

/* Initialization procedures for the ssp data */

static inline void ssp_init_oa(ssp_t * tail, fbprime_t p, fbprime_t r, unsigned int skip, where_am_I_ptr w MAYBE_UNUSED)/*{{{*/
{
    tail->p = p;
    tail->r = r;
    tail->offset = (r * skip) % p;
}/*}}}*/

static inline void ssp_init_op(ssp_bad_t * tail, fbprime_t p, fbprime_t r, unsigned int skip MAYBE_UNUSED, where_am_I_ptr w MAYBE_UNUSED)/*{{{*/
{
    unsigned int v = r; /* have consistent notations */
    unsigned int g = gcd_ul(p, v);
    fbprime_t q = p / g;
    tail->g = g;
    tail->q = q;
    if (q == 1) {
        ASSERT(r == 0);
        tail->U = 0;
    } else {
        int rc;
        uint64_t U = v / g; /* coprime to q */
        rc = invmod(&U, q);
        ASSERT_ALWAYS(rc != 0);
        tail->U = U;
    }
}/*}}}*/

void small_sieve_init(small_sieve_data_t *ssd, const factorbase_degn_t *fb,
                      sieve_info_srcptr si, int side)
{
    const factorbase_degn_t *fb_sav = fb;
    int size = 0;
    const unsigned int thresh = si->bucket_thresh;
    const int verbose = 0;
    const int do_bad_primes = 1;
    where_am_I w;

    // Count prime ideals of factor base primes p < thresh
    while (fb->p != FB_END && fb->p < thresh) {
        size += fb->nr_roots;
        fb = fb_next (fb); // cannot do fb++, due to variable size !
    }
    fb = fb_sav;

    // allocate space for these. n is an upper bound, since some of the
    // ideals might become special ones.
    ssd->ssp = (ssp_t *) malloc(size * sizeof(ssp_t));
    FATAL_ERROR_CHECK(ssd->ssp == NULL, "malloc failed");
    ssd->next_position = NULL;
    ssd->markers = NULL;
    int nmarkers = 0;
    ssd->logp = (unsigned char *) malloc(size);
    // Do another pass on fb and badprimes, to fill in the data
    // while we have any regular primes or bad primes < thresh left
    ssp_t * tail = ssd->ssp;

    int index;

    // The processing of bucket region by nb_threads is interleaved.
    // It means that the positions for the small sieve must jump
    // over the (nb_threads - 1) regions after each region.
    // For typical primes, this jump is easily precomputed and goes into
    // the ssp struct.
    
    unsigned int skiprows = (bucket_region >> si->logI)*(si->nb_threads-1);
    for (index = 0 ; fb->p != FB_END && fb->p < thresh ; fb = fb_next(fb)) {
        const fbprime_t p = fb->p;
        WHERE_AM_I_UPDATE(w, p, p);

        int nr;
        fbprime_t r;

        for (nr = 0; nr < fb->nr_roots; nr++, index++) {
            unsigned int event = 0;
            if ((fb->p&1)==0) event |= SSP_POW2;
            ssd->logp[index] = fb->plog;
            WHERE_AM_I_UPDATE(w, r, fb->roots[nr]);
            r = fb_root_in_qlattice (p, fb->roots[nr], fb->invp, si);
            /* If this root is somehow interesting (projective in (a,b) or
               in (i,j) plane), print a message */
            if (verbose && (fb->roots[nr] >= p || r >= p))
                fprintf (si->output, "# small_sieve_init: %s side, prime " 
                        FBPRIME_FORMAT " root " FBPRIME_FORMAT " -> " 
                        FBPRIME_FORMAT "\n", sidenames[side], p, fb->roots[nr], r);

            /* Handle projective roots */
            if (r >= p) {
                /* Compute the init data in any case, since the gcd
                 * dominates (and anyway we won't be doing this very
                 * often). */
                event |= SSP_PROJ;
                ssp_bad_t * ssp = (ssp_bad_t *) tail;
                ssp_init_op(ssp, p, r - p, skiprows, w);
                /* If g exceeds J, then the only reached locations in the
                 * sieving area will be on line (j=0), thus (1,0) only since
                 * the other are equivalent.
                 */
                if (!do_bad_primes) {
                    if (verbose) {
                        fprintf (si->output,
                                "# small_sieve_init: not adding bad prime"
                                " (1:"FBPRIME_FORMAT") mod "FBPRIME_FORMAT")"
                                " to small sieve because do_bad_primes = 0\n",
                                r-p, p);
                    }
                    event |= SSP_DISCARD;
                } else if (ssp->g >= si->J) {
                    if (verbose) {
                        fprintf (si->output,
                                "# small_sieve_init: not adding bad prime"
                                " (1:"FBPRIME_FORMAT") mod "FBPRIME_FORMAT")"
                                " to small sieve  because g=%d >= si->J = %d\n",
                                r-p, p, ssp->g, si->J);
                    }
                    event |= SSP_DISCARD;
                }
            } else {
                ssp_init_oa(tail, p, r, skiprows, w);
            }
            tail++;
            if (event)
                PUSH_SSP_MARKER(ssd, nmarkers, index, event);
        }
    }
    PUSH_SSP_MARKER(ssd, nmarkers, index, SSP_END);
    ssd->nb_ssp = size;
    ssd_info(si, "small sieve", side, ssd);
}

/* Only compute the initial next_position fields. */
void small_sieve_start(small_sieve_data_t *ssd, unsigned int j0, sieve_info_srcptr si)
{
    ssp_marker_t * next_marker = ssd->markers;
    ssd->next_position = (int *) malloc(ssd->nb_ssp * sizeof(int));

    for(int i = 0 ; i < ssd->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        event = next_marker->event;
        fence = next_marker->index;
        next_marker++;
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(ssd->ssp[i]);
            unsigned int compensate = si->I / 2;
            compensate += j0 * ssp->r;
            ssd->next_position[i] = compensate %ssp->p;
        }
        if (event & SSP_DISCARD) continue;
        if (event & SSP_END) break;
        if (event & SSP_PROJ) {
            ssp_bad_t * ssp = (ssp_bad_t *) &(ssd->ssp[i]);
            /* Compute the next multiple of g above j0 */
            unsigned int j1 = j0 - (j0 % ssp->g);
            unsigned int compensate = si->I / 2;
            if (j0) { /* most often j1 is < j0 -- in this case,
                         the j1 we're looking for needs +g */
                j1 += ssp->g;
            }
            ASSERT(j1 >= j0);
            ASSERT(j1 % ssp->g == 0);
            /* Now we'd like to avoid row number 0 (so j1 == 0).  */
            /* Note that we avoid it entirely -- we could fathom sieving
             * (1,0), but it's probably not really worth it */
            if (j1 == 0) {
                j1 += ssp->g;
            }
            compensate += j1 * ssp->U;
            ssd->next_position[i] = (j1-j0) * si->I + compensate % ssp->q;
        } else if (event & SSP_POW2) {
            /* For powers of 2, we sieve only odd lines (*) and 
             * next_position needs to point at line j=1. We assume
             * that in this case (si->I/2) % p == 0
             * (*) for lines with j even, we have a root mod the prime
             * power for i-j*r multiple of our power of 2, which means
             * i even too. Thus a useless report.
             */
            ssp_t * ssp = &(ssd->ssp[i]);
            /* Note that j0 may perfectly be odd, in the case I==16 ! */
            unsigned int j1 = j0 | 1;
            unsigned int compensate = si->I / 2;
            compensate += j1 * ssp->r;
            ssd->next_position[i] = (j1-j0) * si->I + compensate % ssp->p;
        }
    }
}

/* Skip stride */
void small_sieve_skip_stride(small_sieve_data_t *ssd, unsigned int skip, sieve_info_srcptr si)
{
    if (skip == 0) return;

    ssp_marker_t * next_marker = ssd->markers;

    for(int i = 0 ; i < ssd->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        event = next_marker->event;
        fence = next_marker->index;
        next_marker++;
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(ssd->ssp[i]);
            ssd->next_position[i] += ssp->offset;
            if (ssd->next_position[i] >= (int) ssp->p)
                ssd->next_position[i] -= ssp->p;
        }
        if (event & SSP_DISCARD) continue;
        if (event & SSP_END) break;
        if (event & SSP_PROJ) {
            /* Don't bother. Pay attention to the fact that we have offsets to
             * the (current) bucket base. */
            ssp_bad_t * ssp = (ssp_bad_t *) &(ssd->ssp[i]);
            unsigned int x = ssd->next_position[i];
            const unsigned int I = 1U << si->logI;
            unsigned int imask = I-1;
            unsigned int j = x >> si->logI;
            if (j >= skip) {
                /* The ``next_position'' is still ahead of us, so there's
                 * no adjustment to make */
                x -= skip*I;
            } else {
                /* We've hit something in this bucket, but the
                 * next_position field lands in the blank space between
                 * this bucket and the next one to be handled. So we must
                 * advance: add g to j enough times so that j>=skip.
                 * Which means j+g*ceil((skip-j)/g)
                 */
                unsigned int i = x & imask;
                unsigned int jI = x - i;
                unsigned int nskip = iceildiv(skip-j, ssp->g);
                jI = jI + (nskip * ssp->g - skip) * I;
                i = (i + nskip * ssp->U) % ssp->q;
                x = jI + i;
            }
            ssd->next_position[i] = x;
        } else if (event & SSP_POW2) {
            ssp_t * ssp = &(ssd->ssp[i]);
            ssd->next_position[i] += ssp->offset;
            /* Pay attention to the fact that the moment, next_position
             * may still point to the _second line_ in the area. So we
             * must not cancel the high bits
             */
            // ssd->next_position[i] &= ssp->p - 1;
        }
    }
}

// Update the positions in the small_sieve_data ssd for going up in the
// sieve region by nl lines 
// This takes the position in ref_ssd as a reference.
// For typical primes, and if use_offset is set to 1, one uses the
// precomputed offset to jump without mod p reduction (yet still a
// subtraction, though).
void ssd_update_positions(small_sieve_data_t *ssd, 
        small_sieve_data_t *ref_ssd, sieve_info_ptr si, int nl,
        int use_offset)
{
    const int row0_is_oddj = nl & 1;

    ssp_marker_t * next_marker = ssd->markers;

    for(int i = 0 ; i < ssd->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        event = next_marker->event;
        fence = next_marker->index;
        next_marker++;
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(ssd->ssp[i]);
            unsigned long i0;
            fbprime_t p = ssp->p;
            fbprime_t r = ssp->r;

            /* We want to add nl*r to the offset *relative to the 
               start of the line*, but next_position may be larger 
               than I, so we treat the multiple-of-I and mod-I parts
               separately */
            /* XXX [ET] Hmmm. Can one give me a case, beyond 2, where
             * next_position>I ?  */
            ASSERT(p % 2 == 0 || ssd->next_position[i] < (int) si->I);
            if (use_offset) {
                i0 = ssd->next_position[i] & (si->I - 1);
                ASSERT (i0 < p);
                i0 += ssp->offset;
                if (i0 >= p)
                    i0 -= p;
                ssd->next_position[i] = i0 + 
                    (ssd->next_position[i] & (~(si->I - 1)));
            } else {
                i0 = ref_ssd->next_position[i] & (si->I - 1);
                ASSERT (i0 < p);
                i0 += nl*r;
                i0 = i0 % p;
                ssd->next_position[i] = i0 + 
                    (ref_ssd->next_position[i] & (~(si->I - 1)));
            }
        }
        if (event == SSP_END) {
            ASSERT_ALWAYS(fence == ssd->nb_ssp);
            break;
        }
        if (event & SSP_DISCARD)
            continue;
        if (event & SSP_PROJ) {
            ssp_bad_t * ssp = (ssp_bad_t *) &(ssd->ssp[i]);
            /* First line to sieve is the smallest j with g|j and j >= nl,
               however, if nl == 0 we don't sieve j==0 since it contains
               only one possible relation (i,j) = (1,0). */
            unsigned int ng, x, j;
            ng = iceildiv(nl, ssp->g);
            if (ng == 0)
                ng++;
            x = (si->I / 2 + ng * ssp->U) % ssp->q;
            j = ng * ssp->g;
            ssd->next_position[i] = (j - nl) * si->I + x;
        } else {
            ASSERT_ALWAYS(event & SSP_POW2);
            ssp_t * ssp = &(ssd->ssp[i]);
            unsigned long i0;
            fbprime_t p = ssp->p;
            fbprime_t r = ssp->r;
            if (p == 2) {
                /* Make sure that next_position points to a location
                   where i and j are not both even */
                /* [ET] previous code was overly complicated. */
                i0 = r;
                if (!row0_is_oddj) i0 += si->I;
                ssd->next_position[i] = i0;
            } else {
                if (row0_is_oddj) {
                    i0 = (nl * r) & (p-1);
                } else {
                    i0 = (((nl + 1) * r) & (p-1)) + si->I;
                }
                ssd->next_position[i] = i0;
            }
        }
    }
}

/* This adds extra logging for pattern sieving. Very slow.
 */
#define xxxUGLY_DEBUGGING


// Sieve small primes (up to p < bucket_thresh) of the factor base fb in the
// next sieve region S.
// Information about where we are is in ssd.
void sieve_small_bucket_region(unsigned char *S, int N,
			       small_sieve_data_t * ssd, sieve_info_ptr si,
                               int side,
			       where_am_I_ptr w MAYBE_UNUSED)
{
    const uint32_t I = si->I;
    const fbprime_t pattern2_size = 2 * sizeof(unsigned long);
    unsigned long j;
    const int test_divisibility = 0; /* very slow, but nice for debugging */
    const unsigned long nj = bucket_region >> si->logI; /* Nr. of lines 
                                                           per bucket region */
    /* In order to check whether a j coordinate is even, we need to take
     * into account the bucket number, especially in case buckets are as
     * large as the sieve region. The row number corresponding to a given
     * i0 is i0/I, but we also need to add bucket_nr*bucket_size/I to
     * this, which is what this flag is for.
     */
    const int row0_is_oddj = (N << (LOG_BUCKET_REGION - si->logI)) & 1;


    /* Handle powers of 2 up to 2 * sizeof(long) separately. 
     * TODO: use SSE2 */
    WHERE_AM_I_UPDATE(w, p, 2);
    /* First collect updates for powers of two in a pattern,
       then apply pattern to sieve line.
       Repeat for each line in bucket region. */
    for (j = 0; j < nj; j++)
    {
        WHERE_AM_I_UPDATE(w, j, j);
        unsigned long pattern[2] = {0,0};

        /* Prepare the pattern */

        ssp_marker_t * next_marker = ssd->markers;
        int fence = -1;
        unsigned int event = 0;
        int * interval = si->sides[side]->fb_parts_x->pow2;
        for(int n = interval[0] ; n < interval[1] ; n++) {
            for( ; fence < n || event == SSP_POW2 ; next_marker++) {
                event = next_marker->event;
                fence = next_marker->index;
            }
            if (n < fence) {
                const fbprime_t p = ssd->ssp[n].p;
                unsigned int i0 = ssd->next_position[n];
                if (i0 < I) {
                    ASSERT (i0 < p);
                    ASSERT ((nj * N + j) % 2 == 1);
                    for (unsigned int i = i0; i < pattern2_size; i += p)
                        ((unsigned char *)pattern)[i] += ssd->logp[n];
#ifdef UGLY_DEBUGGING
                    for (unsigned int j = i0; j < I ; j+= p) {
                        WHERE_AM_I_UPDATE(w, x, (w->j << si->logI) + j);
                        sieve_decrease(S + j, ssd->logp[n], w);
                        /* cancel the above action */
                        S[j] += ssd->logp[n];
                    }
#endif
                    /* Skip two lines above, since we sieve only odd lines.
                     * Even lines would correspond to useless reports.
                     */
                    i0 = ((i0 + 2 * ssd->ssp[n].r) & (p - 1)) + 2 * I;
                }
                /* In this loop, next_position gets updated to the first 
                   index to sieve relative to the start of the next line, 
                   but after all lines of this bucket region are processed, 
                   it will point the the first position to sieve relative  
                   to the start of the next bucket region, as required */
                ssd->next_position[n] = i0 - I;
            } else {
                /* nothing. It's a (presumably) projective power of 2,
                 * but for the moment these are not pattern-sieved. */
            }
        }

        /* Apply the pattern */
        if (pattern[0] || pattern[1]) {
            unsigned long *S_ptr = (unsigned long *) (S + j * I);
            const unsigned long *end = (unsigned long *)(S + j * I + I);

#ifdef TRACE_K /* {{{ */
            if (trace_on_range_Nx(w->N, w->j*I, w->j*I+I)) {
                unsigned int x = trace_Nx.x;
                unsigned int k = x % I;
                unsigned int v = (((unsigned char *)(pattern+((k/sizeof(unsigned long))&1)))[k%sizeof(unsigned long)]);
                if (v) {
                    WHERE_AM_I_UPDATE(w, x, x);
                    sieve_decrease_logging(S + x, v, w);
                }
            }
#endif /* }}} */

            while (S_ptr < end)
            {
                *(S_ptr) -= pattern[0];
                *(S_ptr + 1) -= pattern[1];
                *(S_ptr + 2) -= pattern[0];
                *(S_ptr + 3) -= pattern[1];
                S_ptr += 4;
            }
        }
    }


    /* Handle 3 */
    WHERE_AM_I_UPDATE(w, p, 3);
    /* First collect updates for powers of two in a pattern,
       then apply pattern to sieve line.
       Repeat for each line in bucket region. */
    for (j = 0; j < nj; j++)
    {
        WHERE_AM_I_UPDATE(w, j, j);
        unsigned long pattern[3];

        pattern[0] = pattern[1] = pattern[2] = 0UL;

        ssp_marker_t * next_marker = ssd->markers;
        int fence = -1;
        // unsigned int event = 0;
        int * interval = si->sides[side]->fb_parts_x->pow3;
        for(int n = interval[0] ; n < interval[1] ; n++) {
            for( ; fence < n ; next_marker++) {
                // event = next_marker->event;
                fence = next_marker->index;
            }
            if (n < fence) { /* a nice prime */
                ASSERT_ALWAYS(ssd->ssp[n].p == 3);
                const fbprime_t p = 3;
                WHERE_AM_I_UPDATE(w, p, p);
                unsigned int i0 = ssd->next_position[n];
                unsigned int i;
                ASSERT (i0 < p);
                for (i = i0; i < 3 * sizeof(unsigned long); i += p)
                    ((unsigned char *)pattern)[i] += ssd->logp[n];
                i0 += ssd->ssp[n].r;
                if (i0 >= p)
                    i0 -= p;
                ssd->next_position[n] = i0;
            } else {
                /* n points to a power of 3, and we have an exceptional
                 * event. Sure it can neither be SSP_END nor SSP_POW2.
                 * It's thus almost surely SSP_PROJ, although we could
                 * conceivably have SSP_DISCARD as well
                 */
                /* We should / could do something, anyway. Given that at
                 * this point, we have only 3 ulongs for the pattern,
                 * we're certain that a projective prime is trivial*/
                /* TODO */
            }
        }

        if (pattern[0]) {
            unsigned long *S_ptr = (unsigned long *) (S + j * I);
            const unsigned long *end = (unsigned long *)(S + j * I + I) - 2;
            
#ifdef TRACE_K /* {{{ */
            if (trace_on_range_Nx(w->N, w->j*I, w->j*I+I)) {
                unsigned int x = trace_Nx.x;
                unsigned int k = x % I;
                unsigned int v = (((unsigned char *)(pattern+((k/sizeof(unsigned long))%3)))[k%sizeof(unsigned long)]);
                if (v) {
                    WHERE_AM_I_UPDATE(w, x, x);
                    sieve_decrease_logging(S + x, v, w);
                }
            }
#endif /* }}} */

            while (S_ptr < end)
            {
                *(S_ptr) -= pattern[0];
                *(S_ptr + 1) -= pattern[1];
                *(S_ptr + 2) -= pattern[2];
                S_ptr += 3;
            }

            end += 2;
            if (S_ptr < end)
                *(S_ptr++) -= pattern[0];
            if (S_ptr < end)
                *(S_ptr) -= pattern[1];
        }
    }

    ssp_marker_t * next_marker = ssd->markers;

    // sieve with everyone, since pattern-sieving may miss some of the
    // small primes.

    for(int i = 0 ; i < ssd->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        event = next_marker->event;
        fence = next_marker->index;
        next_marker++;
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(ssd->ssp[i]);
            const fbprime_t p = ssp->p;
            const fbprime_t r = ssp->r;
            WHERE_AM_I_UPDATE(w, p, p);
            const unsigned char logp = ssd->logp[i];
            unsigned char *S_ptr = S;
            fbprime_t twop;
            unsigned int linestart = 0;
            /* Always S_ptr = S + linestart. S_ptr is used for the actual array
               updates, linestart keeps track of position relative to start of
               bucket region and is used only for computing i,j-coordinates
               in overflow and divisibility checking, and relation tracing. */

            unsigned int i0 = ssd->next_position[i];

            /* Don't sieve 3 again as it was pattern-sieved -- unless
             * it's projective, but in this branch we have no projective
             * primes. */
            if (p == 3)
                continue;

            ASSERT(i0 < p);
            for (j = 0; j < nj; j++) {
                WHERE_AM_I_UPDATE(w, j, j);
                twop = p;
                unsigned int i = i0;
                if ((((nj & N) ^ j) & 1) == 0) { /* (nj*N+j)%2 */
                    /* for j even, we sieve only odd i. */
                    twop += p;
                    i += (i0 & 1) ? 0 : p;
                }
                for ( ; i < I; i += twop) {
                    WHERE_AM_I_UPDATE(w, x, j * I + i);
                    sieve_decrease (S_ptr + i, logp, w);
                }
                i0 += r;
                if (i0 >= p)
                    i0 -= p;
                S_ptr += I;
                linestart += I;
            }
            ssd->next_position[i] = i0;
        }
        if (event == SSP_END) {
            ASSERT_ALWAYS(fence == ssd->nb_ssp);
            break;
        }
        if (event & SSP_DISCARD)
            continue;
        if (event & SSP_PROJ) {
            ssp_bad_t * ssp = (ssp_bad_t *) &(ssd->ssp[i]);
            const fbprime_t g = ssp->g;
            const fbprime_t q = ssp->q;
            const fbprime_t U = ssp->U;
            const fbprime_t p MAYBE_UNUSED = g * q;
            WHERE_AM_I_UPDATE(w, p, p);
            const unsigned char logp = ssd->logp[i];
            /* Sieve the bad primes. We have p^k | fij(i,j) for i,j such
             * that i * g == j * U (mod p^k) where g = p^l and gcd(U, p)
             * = 1.  This hits only for g|j, then j = j' * g, and i == j'
             * * U (mod p^(k-l)).  In every g-th line, we sieve the
             * entries with i == (j/g)*U (mod q).  In ssd we have stored
             * g, q = p^(k-l), U, and next_position so that S +
             * next_position is the next sieve entry that needs to be
             * sieved.  So if S + next_position is in the current bucket
             * region, we update all  S + next_position + n*q  where
             * next_position + n*q < I, then set next_position =
             * ((next_position % I) + U) % q) + I * g.  */
            if (!test_divisibility && ssp->q == 1)
            {
                /* q = 1, therefore U = 0, and we sieve all entries in lines
                   with g|j, beginning with the line starting at S[next_position] */
                unsigned long logps;
                unsigned int i0 = ssd->next_position[i];
                ASSERT (ssp->U == 0);
                ASSERT (i0 % I == 0);
                ASSERT (I % (4 * sizeof (unsigned long)) == 0);
                for (j = 0; j < sizeof (unsigned long); j++)
                    ((unsigned char *)&logps)[j] = logp;
                while (i0 < (unsigned int) bucket_region)
                {
                    unsigned long *S_ptr = (unsigned long *) (S + i0);
                    unsigned long *end = S_ptr + I / sizeof (unsigned long);
                    unsigned long logps2 = logps;
                    /* Check whether the j coordinate is even. */
                    if (((i0 & I) == 0) ^ row0_is_oddj) {
                        /* Yes, j is even. We update only odd i-coordinates */
                        /* Use array indexing to avoid endianness issues. */
                        for (j = 0; j < sizeof (unsigned long); j += 2)
                            ((unsigned char *)&logps2)[j] = 0;
                    }
#ifdef TRACE_K
                    if (trace_on_range_Nx(w->N, i0, i0 + I)) {
                        WHERE_AM_I_UPDATE(w, x, trace_Nx.x);
                        sieve_decrease_logging(S + w->x, logp, w);
                    }
#endif
                    while (S_ptr < end)
                    {
                        *(S_ptr) -= logps2;
                        *(S_ptr + 1) -= logps2;
                        *(S_ptr + 2) -= logps2;
                        *(S_ptr + 3) -= logps2;
                        S_ptr += 4;
                    }
                    i0 += ssp->g * I;
                }
                ssd->next_position[i] = i0 - (1U << LOG_BUCKET_REGION);
            } else {
                /* q > 1, more general sieving code. */
                const unsigned int i0 = ssd->next_position[i];
                const fbprime_t evenq = (q % 2 == 0) ? q : 2 * q;
                unsigned int lineoffset = i0 & (I - 1U),
                             linestart = i0 - lineoffset;
                ASSERT (U < q);
                while (linestart < (1U << LOG_BUCKET_REGION))
                {
                    WHERE_AM_I_UPDATE(w, j, linestart / I);
                    unsigned int i = lineoffset;
                    if (((linestart & I) == 0) ^ row0_is_oddj) /* Is j even? */
                    {
                        /* Yes, sieve only odd i values */
                        if (i % 2 == 0) /* Make i odd */
                            i += q;
                        if (i % 2 == 1) /* If not both i,q are even */
                            for ( ; i < I; i += evenq)
                            {
                                WHERE_AM_I_UPDATE(w, x, linestart + i);
                                sieve_decrease (S + linestart + i, logp, w);
                            }
                    }
                    else
                    {
                        for ( ; i < I; i += q)
                        {
                            WHERE_AM_I_UPDATE(w, x, linestart + i);
                            sieve_decrease (S + linestart + i, logp, w);
                        }
                    }

                    linestart += g * I;
                    lineoffset += U;
                    if (lineoffset >= q)
                        lineoffset -= q;
                }
                ssd->next_position[i] = linestart + lineoffset - 
                    (1U << LOG_BUCKET_REGION);
            }
        } else if (event & SSP_POW2) {
            /* Powers of 2 are treated separately */
            /* Don't sieve powers of 2 again that were pattern-sieved */
            ssp_t * ssp = &(ssd->ssp[i]);
            const fbprime_t p = ssp->p;
            const fbprime_t r = ssp->r;
            WHERE_AM_I_UPDATE(w, p, p);

            if (p <= pattern2_size)
                continue;

            const unsigned char logp = ssd->logp[i];
            unsigned char *S_ptr = S;
            unsigned int linestart = 0;

            unsigned int i0 = ssd->next_position[i];
            for (j = 0; j < nj; j++) {
                WHERE_AM_I_UPDATE(w, j, j);
                if (i0 < I) {
                    ASSERT(i0 < p);
                    ASSERT ((nj * N + j) % 2 == 1);
                    for (unsigned int i = i0; i < I; i += p) {
                        WHERE_AM_I_UPDATE(w, x, j * I + i);
                        sieve_decrease (S_ptr + i, logp, w);
                    }
                    // odd lines only.
                    i0 = ((i0 + 2 * r) & (p - 1)) + 2 * I;
                }
                i0 -= I;
                linestart += I;
                S_ptr += I;
            }
            ssd->next_position[i] = i0;
        }
    }
}

/* Sieve small primes (p < I, p not in trialdiv_primes list) of the factor
   base fb in the next sieve region S, and add primes and the x position
   where they divide and where there's a sieve report to a bucket (rather
   than subtracting the log norm from S, as during sieving).
   Information about where we are is in ssd.
   Primes in trialdiv_primes must be in increasing order. */
void
resieve_small_bucket_region (bucket_primes_t *BP, int N, unsigned char *S,
        small_sieve_data_t *ssd,
        sieve_info_srcptr si, where_am_I_ptr w MAYBE_UNUSED)
{
    const uint32_t I = si->I;
    unsigned char *S_ptr;
    unsigned long j, nj;
    const int resieve_very_verbose = 0, resieve_very_verbose_bad = 0;
    /* See comment above about the variable of the same name */
    const int row0_is_oddj = (N << (LOG_BUCKET_REGION - si->logI)) & 1;

    nj = (bucket_region >> si->logI);

    ssp_marker_t * next_marker = ssd->markers;

    for(int i = 0 ; i < ssd->nb_ssp ; i++) {
        int fence;
        unsigned int event;
        event = next_marker->event;
        fence = next_marker->index;
        next_marker++;
        for( ; i < fence ; i++) {
            ssp_t * ssp = &(ssd->ssp[i]);
            const fbprime_t p = ssp->p;
            fbprime_t r = ssp->r;
            WHERE_AM_I_UPDATE(w, p, p);
            unsigned int i0 = ssd->next_position[i];
            S_ptr = S;
            ASSERT(i0 < p);
            /* for j even, we sieve only odd i. This translates into loops
             * which look as follows:
             *
             * j even: (sieve only odd i)
             *   for(i = i0 + (p & -!(i0&1)) ; i < I ; i += p+p)
             *   (where (p & -!(i0&1)) is 0 if i0 is odd, and p otherwise)
             * j odd: (sieve all values of i)
             *   for(i = i0                  ; i < I ; i += p)
             *
             * we may merge the two by setting q=p&-!((j&1)^row0_is_oddj)
             *
             * which, when (j+row0_is_oddj) is even, is p, and is 0
             * otherwise.
             *
             * In turn, since q changes for each j, 1 xor within the loop
             * is enough to make it alternate between 0 and p, once the
             * starting value is correct.
             */
            unsigned int q = p&-!row0_is_oddj;
            for (j = 0; j < nj; j ++) {
                WHERE_AM_I_UPDATE(w, j, j);
                for (unsigned int i = i0 + (q& -!(i0&1)) ; i < I; i += p+q) {
                    if (S_ptr[i] == 255) continue;
                    bucket_prime_t prime;
                    unsigned int x = (j << (si->logI)) + i;
                    if (resieve_very_verbose) {
                        pthread_mutex_lock(&io_mutex);
                        fprintf (stderr, "resieve_small_bucket_region: root "
                                FBPRIME_FORMAT ",%d divides at x = "
                                "%d = %lu * %u + %d\n",
                                p, r, x, j, 1 << si->logI, i);
                        pthread_mutex_unlock(&io_mutex);
                    }
                    prime.p = p;
                    prime.x = x;
                    ASSERT(prime.p >= si->td_thresh);
                    push_bucket_prime (BP, prime);
                }
                i0 += r;
                if (i0 >= p)
                    i0 -= p;
                S_ptr += I;
                q ^= p;
            }
            ssd->next_position[i] = i0;
        }
        if (event == SSP_END) {
            break;
        }
        if (event == SSP_DISCARD) {
            continue;
        }
        if (event == SSP_PROJ) {
            ssp_bad_t * ssp = (ssp_bad_t * ) &(ssd->ssp[i]);
            const fbprime_t g = ssp->g;

            WHERE_AM_I_UPDATE(w, p, ssp->g * ssp->q);

            /* Test every p-th line, starting at S[next_position] */
            unsigned int i0 = ssd->next_position[i];
            unsigned int ii;
            ASSERT (i0 % I == 0); /* make sure next_position points at start
                                     of line */
            if (resieve_very_verbose_bad) {
                pthread_mutex_lock(&io_mutex);
                fprintf (stderr, "# resieving bad prime " FBPRIME_FORMAT
                        ", i0 = %u\n", g, i0);
                pthread_mutex_unlock(&io_mutex);
            }
            while (i0 < (unsigned int) bucket_region) {
                unsigned char *S_ptr = S + i0;
                if ((i0 >> si->logI) % 2 == 0) { /* Even j coordinate? */
                    /* Yes, test only odd ii-coordinates */
                    for (ii = 1; ii < I; ii += 2) {
                        if (S_ptr[ii] != 255) {
                            bucket_prime_t prime;
                            const unsigned int x = i0 + ii;
                            if (resieve_very_verbose_bad) {
                                pthread_mutex_lock(&io_mutex);
                                fprintf (stderr, "resieve_small_bucket_region even j: root "
                                        FBPRIME_FORMAT ",inf divides at x = %u\n",
                                        g, x);
                                pthread_mutex_unlock(&io_mutex);
                            }
                            prime.p = g;
                            prime.x = x;
                            ASSERT(prime.p >= si->td_thresh);
                            push_bucket_prime (BP, prime);
                        }
                    }
                } else {
                    /* No, test all ii-coordinates */
                    for (ii = 0; ii < I; ii++) {
                        if (S_ptr[ii] != 255) {
                            bucket_prime_t prime;
                            const unsigned int x = i0 + ii;
                            if (resieve_very_verbose_bad) {
                                pthread_mutex_lock(&io_mutex);
                                fprintf (stderr, "resieve_small_bucket_region odd j: root "
                                        FBPRIME_FORMAT ",inf divides at x = %u\n",
                                        g, x);
                                pthread_mutex_unlock(&io_mutex);
                            }
                            prime.p = g;
                            prime.x = x;
                            ASSERT(prime.p >= si->td_thresh);
                            push_bucket_prime (BP, prime);
                        }
                    }
                }
                i0 += g * I;
            }
            ssd->next_position[i] = i0 - bucket_region;
            if (resieve_very_verbose_bad) {
                pthread_mutex_lock(&io_mutex);
                fprintf (stderr, "# resieving: new i0 = %u, bucket_region = %d, "
                        "new next_position = %d\n",
                        i0, bucket_region, ssd->next_position[i]);
                pthread_mutex_unlock(&io_mutex);
            }
        }
    }
}
/* }}} */

/* {{{ Trial division */
typedef struct {
    uint64_t *fac;
    int n;
} factor_list_t;

#define FL_MAX_SIZE 200

void factor_list_init(factor_list_t *fl) {
    fl->fac = (uint64_t *) malloc (FL_MAX_SIZE * sizeof(uint64_t));
    ASSERT_ALWAYS(fl->fac != NULL);
    fl->n = 0;
}

void factor_list_clear(factor_list_t *fl) {
    free(fl->fac);
}

static void 
factor_list_add(factor_list_t *fl, const uint64_t p)
{
  ASSERT_ALWAYS(fl->n < FL_MAX_SIZE);
  fl->fac[fl->n++] = p;
}

// print a comma-separated list of factors.
// returns the number of factor printed (in particular, a comma is needed
// after this output only if the return value is non zero)
int factor_list_fprint(FILE *f, factor_list_t fl) {
    int i;
    for (i = 0; i < fl.n; ++i) {
        if (i) fprintf(f, ",");
        fprintf(f, "%" PRIx64, fl.fac[i]);
    }
    return i;
}


static const int bucket_prime_stats = 0;
static long nr_bucket_primes = 0;
static long nr_div_tests = 0;
static long nr_composite_tests = 0;
static long nr_wrap_was_composite = 0;
/* The entries in BP must be sorted in order of increasing x */
static void
divide_primes_from_bucket (factor_list_t *fl, mpz_t norm, const unsigned int N MAYBE_UNUSED, const int x,
                           bucket_primes_t *BP, const unsigned long fbb)
{
  bucket_prime_t prime;
  while (!bucket_primes_is_end (BP)) {
      prime = get_next_bucket_prime (BP);
      if (prime.x > x)
        {
          rewind_primes_by_1 (BP);
          break;
        }
      if (prime.x == x) {
          if (bucket_prime_stats) nr_bucket_primes++;
          unsigned long p = prime.p;
          while (p <= fbb) {
              if (bucket_prime_stats) nr_div_tests++;
              if (mpz_divisible_ui_p (norm, p)) {
                  int isprime;
                  modulusul_t m; 
                  modul_initmod_ul (m, (unsigned long) p);
                  if (bucket_prime_stats) nr_composite_tests++;
                  isprime = modul_isprime (m);
                  modul_clearmod (m);
                  if (isprime) {
                      break;
                  } else {
                    if (bucket_prime_stats) nr_wrap_was_composite++;
                  }
              }

              /* It may have been a case of incorrectly reconstructing p
                 from bits 1...16, so let's try if a bigger prime works.

                 Warning: this strategy may fail, since we might find a
                 composite p+k1*BUCKET_P_WRAP dividing the norm, while we
                 really want a larger prime p+k2*BUCKET_P_WRAP. In that case,
                 if a prime dividing p+k1*BUCKET_P_WRAP also divides the norm,
                 it might lead to a bucket error (p = ... does not divide),
                 moreover the wanted prime p+k2*BUCKET_P_WRAP will not be found
                 and we might miss some relations. */
              p += BUCKET_P_WRAP;
          }
          if (p > fbb) {
              pthread_mutex_lock(&io_mutex);
              fprintf (stderr,
                       "# Error, p = %lu does not divide at (N,x) = (%u,%d)\n",
                       (unsigned long) prime.p, N, x);
              pthread_mutex_unlock(&io_mutex);
              abort();
          }
          do {
              factor_list_add(fl, p);
              mpz_divexact_ui (norm, norm, p);
          } while (mpz_divisible_ui_p (norm, p));
      }
  }
}


NOPROFILE_STATIC void
trial_div (factor_list_t *fl, mpz_t norm, const unsigned int N, int x,
           factorbase_degn_t *fb, bucket_primes_t *primes,
	   trialdiv_divisor_t *trialdiv_data, const unsigned long fbb,
           int64_t a MAYBE_UNUSED, uint64_t b MAYBE_UNUSED)
{
    const int trial_div_very_verbose = 0;
    int nr_factors;
    fl->n = 0; /* reset factor list */

    if (trial_div_very_verbose) {
        pthread_mutex_lock(&io_mutex);
        gmp_fprintf (stderr, "# trial_div() entry, x = %d, norm = %Zd\n",
                x, norm);
        pthread_mutex_unlock(&io_mutex);
    }

    // handle 2 separately, if it is in fb
    if (fb->p == 2) {
        int bit = mpz_scan1(norm, 0);
        int i;
        for (i = 0; i < bit; ++i) {
            fl->fac[fl->n] = 2;
            fl->n++;
        }
        if (trial_div_very_verbose) {
            pthread_mutex_lock(&io_mutex);
            gmp_fprintf (stderr, "# x = %d, dividing out 2^%d, norm = %Zd\n",
                    x, bit, norm);
            pthread_mutex_unlock(&io_mutex);
        }
        mpz_tdiv_q_2exp(norm, norm, bit);
        fb = fb_next (fb); // cannot do fb++, due to variable size !
    }

    // remove primes in "primes" that map to x
    divide_primes_from_bucket (fl, norm, N, x, primes, fbb);
#ifdef TRACE_K /* {{{ */
    if (trace_on_spot_ab(a,b) && fl->n) {
        fprintf(stderr, "# divided by 2 + primes from bucket that map to %u: ", x);
        if (!factor_list_fprint(stderr, *fl)) fprintf(stderr, "(none)");
        gmp_fprintf(stderr, ", remaining norm is %Zd\n", norm);
    }
#endif /* }}} */
    if (trial_div_very_verbose) {
        pthread_mutex_lock(&io_mutex);
        gmp_fprintf (stderr, "# x = %d, after dividing out bucket/resieved norm = %Zd\n",
                x, norm);
        pthread_mutex_unlock(&io_mutex);
    }

    do {
      /* Trial divide primes with precomputed tables */
#define TRIALDIV_MAX_FACTORS 32
      int i;
      unsigned long factors[TRIALDIV_MAX_FACTORS];
      if (trial_div_very_verbose)
      {
          pthread_mutex_lock(&io_mutex);
          fprintf (stderr, "# Trial division by ");
          for (i = 0; trialdiv_data[i].p != 1; i++)
              fprintf (stderr, " %lu", trialdiv_data[i].p);
          fprintf (stderr, "\n");
          pthread_mutex_unlock(&io_mutex);
      }

      nr_factors = trialdiv (factors, norm, trialdiv_data, TRIALDIV_MAX_FACTORS);

      for (i = 0; i < MIN(nr_factors, TRIALDIV_MAX_FACTORS); i++)
      {
          if (trial_div_very_verbose) {
              pthread_mutex_lock(&io_mutex);
              fprintf (stderr, " %lu", factors[i]);
              pthread_mutex_unlock(&io_mutex);
          }
          factor_list_add (fl, factors[i]);
      }
      if (trial_div_very_verbose) {
          pthread_mutex_lock(&io_mutex);
          gmp_fprintf (stderr, "\n# After trialdiv(): norm = %Zd\n", norm);
          pthread_mutex_unlock(&io_mutex);
      }
    } while (nr_factors == TRIALDIV_MAX_FACTORS + 1);
}
/* }}} */

/* {{{ cofactoring area */

/* Return 0 if the leftover norm n cannot yield a relation.
   FIXME: need to check L^k < n < B^(k+1) too.

   Possible cases, where qj represents a prime in [B,L], and rj a prime > L:
   (0) n >= 2^mfb
   (a) n < L:           1 or q1
   (b) L < n < B^2:     r1 -> cannot yield a relation
   (c) B^2 < n < B*L:   r1 or q1*q2
   (d) B*L < n < L^2:   r1 or q1*q2 or q1*r2
   (e) L^2 < n < B^3:   r1 or q1*r2 or r1*r2 -> cannot yield a relation
   (f) B^3 < n < B^2*L: r1 or q1*r2 or r1*r2 or q1*q2*q3
   (g) B^2*L < n < L^3: r1 or q1*r2 or r1*r2
   (h) L^3 < n < B^4:   r1 or q1*r2, r1*r2 or q1*q2*r3 or q1*r2*r3 or r1*r2*r3
*/
static int
check_leftover_norm (mpz_t n, size_t lpb, mpz_t BB, mpz_t BBB, mpz_t BBBB,
                     size_t mfb)
{
  size_t s = mpz_sizeinbase (n, 2);

  if (s > mfb)
    return 0; /* n has more than mfb bits, which is the given limit */
  /* now n < 2^mfb */
  if (s <= lpb)
    return 1; /* case (a) */
  /* now n >= L=2^lpb */
  if (mpz_cmp (n, BB) < 0)
    return 0; /* case (b) */
  /* now n >= B^2 */
  if (2 * lpb < s)
    {
      if (mpz_cmp (n, BBB) < 0)
        return 0; /* case (e) */
      if (3 * lpb < s && mpz_cmp (n, BBBB) < 0)
        return 0; /* case (h) */
    }
  if (mpz_probab_prime_p (n, 1))
    return 0; /* n is a pseudo-prime larger than L */
  return 1;
}

/* This structure will be dropped eventually. factor_survivors does not
 * need its members (only S) */
struct local_sieve_data_s {
    unsigned char * S;      /* local sieve array */
    small_sieve_data_t lsrsd[1];
    /* extra copies */
    small_sieve_data_t lssd[1];
    small_sieve_data_t rssd[1];
};
typedef struct local_sieve_data_s local_sieve_data[1];
typedef struct local_sieve_data_s * local_sieve_data_ptr;
typedef const struct local_sieve_data_s * local_sieve_data_srcptr;


/* Adds the number of sieve reports to *survivors,
   number of survivors with coprime a, b to *coprimes */

NOPROFILE_STATIC int
factor_survivors (thread_data_ptr th, int N, local_sieve_data * loc, where_am_I_ptr w MAYBE_UNUSED)
{
    sieve_info_ptr si = th->si;
    cado_poly_ptr cpoly = si->cpoly;
    sieve_side_info_ptr rat = si->sides[RATIONAL_SIDE];
    sieve_side_info_ptr alg = si->sides[ALGEBRAIC_SIDE];

    int cpt = 0;
    int surv = 0, copr = 0;
    mpz_t norm[2], BB[2], BBB[2], BBBB[2];
    factor_list_t factors[2];
    mpz_array_t *f[2] = { NULL, };
    uint32_array_t *m[2] = { NULL, }; /* corresponding multiplicities */
    bucket_primes_t primes[2];

    mpz_t BLPrat;       /* alone ? */

    uint32_t cof_rat_bitsize = 0; /* placate gcc */
    uint32_t cof_alg_bitsize = 0; /* placate gcc */

    for(int side = 0 ; side < 2 ; side++) {
        f[side] = alloc_mpz_array (8);
        m[side] = alloc_uint32_array (8);

        factor_list_init(&factors[side]);
        mpz_init (norm[side]);
        mpz_init (BB[side]);
        mpz_init (BBB[side]);
        mpz_init (BBBB[side]);

        unsigned long lim = (side == RATIONAL_SIDE) ? cpoly->rat->lim : cpoly->alg->lim;
        mpz_ui_pow_ui (BB[side], lim, 2);
        mpz_mul_ui (BBB[side], BB[side], lim);
        mpz_mul_ui (BBBB[side], BBB[side], lim);
    }

    mpz_init (BLPrat);
    mpz_set_ui (BLPrat, cpoly->rat->lim);
    mpz_mul_2exp (BLPrat, BLPrat, cpoly->rat->lpb); /* fb bound * lp bound */

    unsigned char * alg_S = loc[ALGEBRAIC_SIDE]->S;
    unsigned char * rat_S = loc[RATIONAL_SIDE]->S;
#ifdef TRACE_K /* {{{ */
    if (trace_on_spot_Nx(N, trace_Nx.x)) {
        fprintf(stderr, "# When entering factor_survivors for bucket %u, alg_S[%u]=%u, rat_S[%u]=%u\n",
                trace_Nx.N, trace_Nx.x, alg_S[trace_Nx.x], trace_Nx.x, rat_S[trace_Nx.x]);
    }
#endif  /* }}} */

    /* XXX: Don't believe that resieve_start is easily changeable... */
    const int resieve_start = RATIONAL_SIDE;
    unsigned char * S = loc[resieve_start]->S;

#ifdef UNSIEVE_NOT_COPRIME
    unsieve_not_coprime (S, N, si);
#endif
    
    for (int x = 0; x < bucket_region; ++x)
      {
#ifdef TRACE_K /* {{{ */
          if (trace_on_spot_Nx(N, x)) {
              fprintf(stderr, "# alg->Bound[%u]=%u, rat->Bound[%u]=%u\n",
                      alg_S[trace_Nx.x], alg->Bound[alg_S[x]],
                      rat_S[trace_Nx.x], rat->Bound[rat_S[x]]);
          }
#endif /* }}} */
        unsigned int X;
        unsigned int i, j;

        if (!sieve_info_test_lognorm(alg->Bound, rat->Bound, alg_S[x], rat_S[x], 126))
          {
            S[x] = 255;
            continue;
          }
        th->rep->survivor_sizes[rat_S[x]][alg_S[x]]++;
        surv++;

        X = x + (N << LOG_BUCKET_REGION);
        i = abs ((int) (X & (si->I - 1)) - si->I / 2);
        j = X >> si->logI;
#ifndef UNSIEVE_NOT_COPRIME
        if (bin_gcd_safe (i, j) != 1)
          {
#ifdef TRACE_K
            if (trace_on_spot_Nx(N, x)) {
                fprintf(stderr, "# Slot [%u] in bucket %u has non coprime (i,j)=(%d,%u)\n",
                        trace_Nx.x, trace_Nx.N, i, j);
            }
#endif
            S[x] = 255;
            continue;
          }
#endif
      }

    /* Copy those bucket entries that belong to sieving survivors and
       store them with the complete prime */
    /* FIXME: choose a sensible size here */

    for(int z = 0 ; z < 2 ; z++) {
        int side = resieve_start ^ z;
        WHERE_AM_I_UPDATE(w, side, side);
        primes[side] = init_bucket_primes (bucket_region);

        for (int i = 0; i < si->nb_threads; ++i) {
            thread_data_ptr other = th + i - th->id;
            purge_bucket (&primes[side], other->sides[side]->BA, N, S);
        }

        /* Resieve small primes for this bucket region and store them 
           together with the primes recovered from the bucket updates */
        resieve_small_bucket_region (&primes[side], N, S, loc[side]->lsrsd, si, w);

        /* Sort the entries to avoid O(n^2) complexity when looking for
           primes during trial division */
        bucket_sortbucket (&primes[side]);
    }

    /* Scan array one long word at a time. If any byte is <255, i.e. if
       the long word is != 0xFFFF...FF, examine the bytes */
    for (int xul = 0; xul < bucket_region; xul += sizeof (unsigned long)) {
#ifdef TRACE_K
        if ((unsigned int) N == trace_Nx.N && (unsigned int) xul <= trace_Nx.x && (unsigned int) xul + sizeof (unsigned long) > trace_Nx.x) {
            fprintf(stderr, "# Slot [%u] in bucket %u has value %u\n",
                    trace_Nx.x, trace_Nx.N, S[trace_Nx.x]);
        }
#endif
        if (*(unsigned long *)(S + xul) == (unsigned long)(-1L)) 
            continue;
        for (int x = xul; x < xul + (int) sizeof (unsigned long); ++x) {
            if (S[x] == 255) continue;

            int64_t a;
            uint64_t b;

            // Compute algebraic and rational norms.
            NxToAB (&a, &b, N, x, si);

#ifdef TRACE_K
            if (trace_on_spot_ab(a, b)) {
                fprintf(stderr, "# about to print relation for (%"PRId64",%"PRIu64")\n",a,b);
            }
#endif
            /* since a,b both even were not sieved, either a or b should be odd */
            // ASSERT((a | b) & 1);
            if (UNLIKELY(((a | b) & 1) == 0))
            {
                pthread_mutex_lock(&io_mutex);
                fprintf (stderr, "# Error: a and b both even for N = %d, x = %d,\n"
                        "i = %d, j = %d, a = %ld, b = %lu\n",
                        N, x, ((x + N*bucket_region) & (si->I - 1))
                        - (si->I >> 1),
                        (x + N*bucket_region) >> si->logI,
                        (long) a, (unsigned long) b);
                abort();
                pthread_mutex_unlock(&io_mutex);
            }

            /* Since the q-lattice is exactly those (a, b) with
               a == rho*b (mod q), q|b  ==>  q|a  ==>  q | gcd(a,b) */
            if (b == 0 || (b >= si->q && b % si->q == 0))
                continue;

            copr++;

            /* For hunting missed relations */
#if 0
            if (a == -6537753 && b == 1264)
                fprintf (stderr, "# Have relation %ld,%lu at bucket nr %d, "
                        "x = %d, K = %lu\n", 
                        a, b, N, x, (unsigned long) N * bucket_region + x);
#endif

            int pass = 1;

            for(int z = 0 ; pass && z < 2 ; z++) {
                int side = RATIONAL_SIDE ^ z;   /* start with rational */
                mpz_t * f = cpoly->pols[side]->f;
                int deg = cpoly->pols[side]->degree;
                int lim = cpoly->pols[side]->lim;
                int lpb = cpoly->pols[side]->lpb;
                int mfb = cpoly->pols[side]->mfb;

                // Trial divide rational norm
                mp_poly_homogeneous_eval_siui (norm[side], f, deg, a, b);
                if (si->ratq == (side == RATIONAL_SIDE))
                    mpz_divexact_ui (norm[side], norm[side], si->q);
#ifdef TRACE_K
                if (trace_on_spot_ab(a, b)) {
                    gmp_fprintf(stderr, "# start trial division for norm=%Zd on %s side for (%"PRId64",%"PRIu64")\n",norm[side],sidenames[side],a,b);
                }
#endif
                trial_div (&factors[side], norm[side], N, x,
                        si->sides[side]->fb,
                        &primes[side], si->sides[side]->trialdiv_data,
                        lim, a, b);

                pass = check_leftover_norm (norm[side], lpb,
                                         BB[side], BBB[side], BBBB[side], mfb);
#ifdef TRACE_K
                if (trace_on_spot_ab(a, b)) {
                    gmp_fprintf(stderr, "# checked leftover norm=%Zd on %s side for (%"PRId64",%"PRIu64"): %d\n",norm[side],sidenames[side],a,b,pass);
                }
#endif
            }
            if (!pass) continue;

            if (stats != 0)
              {
                cof_rat_bitsize = mpz_sizeinbase (norm[RATIONAL_SIDE], 2);
                cof_alg_bitsize = mpz_sizeinbase (norm[ALGEBRAIC_SIDE], 2);
                if (stats == 1) /* learning phase */
                  /* no need to use a mutex here: either we use one thread only
                     to compute the cofactorization data and if several threads
                     the order is irrelevant. The only problem that can happen
                     is when two threads increase the value at the same time,
                     and it is increased by 1 instead of 2, but this should
                     happen rarely. */
                  cof_call[cof_rat_bitsize][cof_alg_bitsize] ++;
                else /* stats == 2: we use the learning data */
                  {
                    /* we store the initial number of cofactorization calls in
                       cof_call[0][0] and the remaining nb in cof_succ[0][0] */
                    cof_call[0][0] ++;
                    /* Warning: the <= also catches cases when succ=call=0 */
                    if ((double) cof_succ[cof_rat_bitsize][cof_alg_bitsize] <
                        (double) cof_call[cof_rat_bitsize][cof_alg_bitsize] *
                        stats_prob)
                      continue;
                    cof_succ[0][0] ++;
                  }
              }

            /* if norm[RATIONAL_SIDE] is above BLPrat, then it might not
             * be smooth. We factor it first. Otherwise we factor it
             * last.
             */
            int first = mpz_cmp(norm[RATIONAL_SIDE], BLPrat) > 0 ? RATIONAL_SIDE : ALGEBRAIC_SIDE;

            for(int z = 0 ; pass && z < 2 ; z++) {
                int side = first ^ z;
                int rat = (side == RATIONAL_SIDE);
                int lpb = rat ? cpoly->rat->lpb : cpoly->alg->lpb;
                pass = factor_leftover_norm(norm[side], lpb, f[side], m[side], si->strategy);
            }
            if (!pass) continue;

            /* yippee: we found a relation! */

            if (stats == 1) /* learning phase */
              cof_succ[cof_rat_bitsize][cof_alg_bitsize] ++;

#ifdef UNSIEVE_NOT_COPRIME
            ASSERT (bin_gcd_safe (a, b) == 1);
#endif

            relation_t rel[1];
            memset(rel, 0, sizeof(rel));
            rel->a = a;
            rel->b = b; 
            for (int side = 0; side < 2; side++) {
                for(int i = 0 ; i < factors[side].n ; i++)
                    relation_add_prime(rel, side, factors[side].fac[i]);
                for (unsigned int i = 0; i < f[side]->length; ++i) {
                    if (!mpz_fits_ulong_p(f[side]->data[i]))
                        fprintf(stderr, "Warning: misprinted relation because of large prime at (%"PRId64",%"PRIu64")\n",a,b);
                    for (unsigned int j = 0; j < m[side]->data[i]; j++) {
                        relation_add_prime(rel, side, mpz_get_ui(f[side]->data[i]));
                    }
                }
            }
            relation_compress_rat_primes(rel);
            relation_compress_alg_primes(rel);

#ifdef TRACE_K
            if (trace_on_spot_ab(a, b)) {
                fprintf(stderr, "# Relation for (%"PRId64",%"PRIu64") printed\n", a, b);
            }
#endif
            if (!si->bench) {
                pthread_mutex_lock(&io_mutex);
#if 0
                fprint_relation(si->output, rel);
#else
                /* This code will be dropped soon. The thing is
                 * that las is a moving target for the moment, and
                 * going through the fprint_relation code above changes
                 * the order of factors in printed relations. It's not so
                 * handy.
                 */
                fprintf (si->output, "%" PRId64 ",%" PRIu64, a, b);
                for(int z = 0 ; z < 2 ; z++) {
                    int side = RATIONAL_SIDE ^ z;
                    fprintf (si->output, ":");
                    int comma = factor_list_fprint (si->output, factors[side]);
                    for (unsigned int i = 0; i < f[side]->length; ++i) {
                        for (unsigned int j = 0; j < m[side]->data[i]; j++) {
                            if (comma++) fprintf (si->output, ",");
                            gmp_fprintf (si->output, "%Zx", f[side]->data[i]);
                        }
                    }
                    if (si->ratq == (side == RATIONAL_SIDE)) {
                        if (comma++) fprintf (si->output, ",");
                        fprintf (si->output, "%" PRIx64 "", si->q);
                    }
                }
                fprintf (si->output, "\n");
                fflush (si->output);
#endif
                pthread_mutex_unlock(&io_mutex);
            }
            clear_relation(rel);
            cpt++;
            /* Build histogram of lucky S[x] values */
            th->rep->report_sizes[loc[RATIONAL_SIDE]->S[x]][loc[ALGEBRAIC_SIDE]->S[x]]++;
        }
    }

    th->rep->survivors1 += surv;
    th->rep->survivors2 += copr;

    mpz_clear (BLPrat);

    for(int side = 0 ; side < 2 ; side++) {
        clear_bucket_primes (&primes[side]);
        mpz_clear (BBBB[side]);
        mpz_clear (BBB[side]);
        mpz_clear (BB[side]);
        mpz_clear(norm[side]);
        factor_list_clear(&factors[side]);
        clear_uint32_array (m[side]);
        clear_mpz_array (f[side]);
    }

    return cpt;
}

/* }}} */

/****************************************************************************/

/************************ cofactorization ********************************/

/* FIXME: the value of 20 seems large. Normally, a few Miller-Rabin passes
   should be enough. See also http://www.trnicely.net/misc/mpzspsp.html */
#define NMILLER_RABIN 1 /* in the worst case, what can happen is that a
                           composite number is declared as prime, thus
                           a relation might be missed, but this will not
                           affect correctness */
#define IS_PROBAB_PRIME(X) (0 != mpz_probab_prime_p((X), NMILLER_RABIN))
#define BITSIZE(X)      (mpz_sizeinbase((X), 2))
#define NFACTORS        8 /* maximal number of large primes */

/* This function was contributed by Jerome Milan (and bugs were introduced
   by Paul Zimmermann :-).
   Input: n - the number to be factored (leftover norm). Must be composite!
          l - (large) prime bit size bound is L=2^l
   Assumes n > 0.
   Return value:
          0 if n has a prime factor larger than 2^l
          1 if all prime factors of n are < 2^l
   Output:
          the prime factors of n are factors->data[0..factors->length-1],
          with corresponding multiplicities multis[0..factors->length-1].
*/
int
factor_leftover_norm (mpz_t n, unsigned int l,
                      mpz_array_t* const factors, uint32_array_t* const multis,
		      facul_strategy_t *strategy)
{
  uint32_t i, nr_factors;
  unsigned long ul_factors[16];
  int facul_code;

  factors->length = 0;
  multis->length = 0;

  /* factoring programs do not like 1 */
  if (mpz_cmp_ui (n, 1) == 0)
    return 1;

  /* If n < L, we know that n is prime, since all primes < B have been
     removed, and L < B^2 in general, where B is the factor base bound,
     thus we only need a primality test when n > L. */
  if (BITSIZE(n) <= l)
    {
      append_mpz_to_array (factors, n);
      append_uint32_to_array (multis, 1);
      return 1;
    }
/* Input is required to be composite!
  else if (IS_PROBAB_PRIME(n))
    {
      return 0;
    }
*/

  /* use the facul library */
  // gmp_printf ("facul: %Zd\n", n);
  facul_code = facul (ul_factors, n, strategy);

  if (facul_code == FACUL_NOT_SMOOTH)
    return 0;

  ASSERT (facul_code == 0 || mpz_cmp_ui (n, ul_factors[0]) != 0);

  if (facul_code > 0)
    {
      nr_factors = facul_code;
      for (i = 0; i < nr_factors; i++)
	{
	  unsigned long r;
	  mpz_t t;
	  if (ul_factors[i] > (1UL << l)) /* Larger than large prime bound? */
	    return 0;
	  r = mpz_tdiv_q_ui (n, n, ul_factors[i]);
	  ASSERT_ALWAYS (r == 0UL);
	  mpz_init (t);
	  mpz_set_ui (t, ul_factors[i]);
	  append_mpz_to_array (factors, t);
	  mpz_clear (t);
	  append_uint32_to_array (multis, 1); /* FIXME, deal with repeated
						 factors correctly */
	}

      if (mpz_cmp_ui (n, 1UL) == 0)
	return 1;
      unsigned int s = BITSIZE(n);
      if (s <= l)
        {
          append_mpz_to_array (factors, n);
          append_uint32_to_array (multis, 1);
          return 1;
        }
      /* If we still have more than two primes (or something non-smooth),
         bail out */
      if (s > 2*l)
        return 0;
      /* We always abort below, so let's skip the prp test
      if (IS_PROBAB_PRIME(n))
        return 0; */
    }
  /* When sieving for 3 large primes, here are so many left over, non-smooth
     numbers here that factoring them all takes a long time, for few
     additional relations */
  return 0;
}

/* th->id gives the number of the thread: it is supposed to deal with the set
 * of bucket_regions corresponding to that number, ie those that are
 * congruent to id mod nb_thread.
 *
 * The other threads are accessed by combining the thread pointer th and
 * the thread id: the i-th thread is at th - id + i
 */
void *
process_bucket_region(thread_data_ptr th)
{
    where_am_I w MAYBE_UNUSED;
    sieve_info_ptr si = th->si;

    WHERE_AM_I_UPDATE(w, si, si);

    las_report_ptr rep = th->rep;

    local_sieve_data loc[2];
    memset(loc, 0, sizeof(loc));
    WHERE_AM_I_UPDATE(w, N, th->id);

    small_sieve_data_t ssd[2][1];

    local_sieve_data_ptr lrat = loc[RATIONAL_SIDE];
    local_sieve_data_ptr lalg = loc[ALGEBRAIC_SIDE];

    unsigned int my_row0 = (bucket_region >> si->logI) * th->id;
    unsigned int skiprows = (bucket_region >> si->logI)*(si->nb_threads-1);

    for(int side = 0 ; side < 2 ; side++) {
        local_sieve_data_ptr lo = loc[side];
        sieve_side_info_ptr s = si->sides[side];

        small_sieve_init(ssd[side], si->sides[side]->fb, si, side);
        small_sieve_start(ssd[side], my_row0, si);

        /* make copies of small sieve data: the "next_position" field is
         * specific to each thread.  */
        small_sieve_clone (lo->lssd, ssd[side]);

        /* Yet another copy: used in factor_survivors for resieving small
         * primes */
        init_resieve (lo->lsrsd, lo->lssd, s->trialdiv_primes);
        ssd_info(si, "resieve", side, lo->lsrsd);

        /* A third copy? 
         * TODO: come on! we should be able to do it with less copies 
         */
        small_sieve_clone (lo->rssd, lo->lsrsd);

        /* local sieve region */
        lo->S = (unsigned char *) malloc(bucket_region);
        ASSERT_ALWAYS (lo->S != NULL);
    }

    /* loop over appropriate set of sieve regions */
    for (int i = th->id; i < si->nb_buckets; i += si->nb_threads) 
      {
        WHERE_AM_I_UPDATE(w, side, RATIONAL_SIDE);
        WHERE_AM_I_UPDATE(w, N, i);

        /* Init rational norms */
        rep->tn[RATIONAL_SIDE] -= seconds ();
        init_rat_norms_bucket_region(lrat->S, i, si);
        rep->tn[RATIONAL_SIDE] += seconds ();

        /* Apply rational buckets */
        rep->ttsm -= seconds();
        for (int j = 0; j < si->nb_threads; ++j)  {
            thread_data_ptr ot = th + j - th->id;
            apply_one_bucket(lrat->S, ot->sides[RATIONAL_SIDE]->BA, i, w);
        }
        rep->ttsm += seconds();

        /* Sieve small rational primes */
        sieve_small_bucket_region(lrat->S, i, lrat->lssd, si, RATIONAL_SIDE, w);
	

        WHERE_AM_I_UPDATE(w, side, ALGEBRAIC_SIDE);

        /* Init algebraic norms */
        rep->tn[ALGEBRAIC_SIDE] -= seconds ();
        /* XXX Only the survivors of the rational sieve are initialized */
        rep->survivors0 += init_alg_norms_bucket_region(lalg->S, lrat->S, i, si);
        rep->tn[ALGEBRAIC_SIDE] += seconds ();

        /* Apply algebraic buckets */
        rep->ttsm -= seconds();
        for (int j = 0; j < si->nb_threads; ++j) {
            thread_data_ptr ot = th + j - th->id;
            apply_one_bucket(lalg->S, ot->sides[ALGEBRAIC_SIDE]->BA, i, w);
        }
        rep->ttsm += seconds();

        /* Sieve small algebraic primes */
        sieve_small_bucket_region(lalg->S, i, lalg->lssd, si, ALGEBRAIC_SIDE, w);

        /* Factor survivors */
        rep->ttf -= seconds ();
        rep->reports += factor_survivors (th, i, loc, w);
        rep->ttf += seconds ();

        for(int side = 0 ; side < 2 ; side++) {
            local_sieve_data_ptr lo = loc[side];
            small_sieve_skip_stride(lo->lssd,  skiprows, si);
            small_sieve_skip_stride(lo->lsrsd, skiprows, si);
        }
      }

    /* clear */
    for(int side = 0 ; side < 2 ; side++) {
        local_sieve_data_ptr lo = loc[side];
        free(lo->S);
        small_sieve_clear(lo->lsrsd);
        small_sieve_clear_cloned(lo->lssd);
        small_sieve_clear_cloned(lo->rssd);
        small_sieve_clear(ssd[side]);
    }

    return NULL;
}

static thread_data * thread_data_alloc(sieve_info_ptr si)
{
    thread_data * thrs = (thread_data *) malloc(si->nb_threads * sizeof(thread_data));
    ASSERT_ALWAYS(thrs);
    memset(thrs, 0, si->nb_threads * sizeof(thread_data));

    for(int i = 0 ; i < si->nb_threads ; i++) {
        thrs[i]->id = i;
        thrs[i]->si = si;
    }

    for(int z = 0 ; z < 2 ; z++) {
        int side = ALGEBRAIC_SIDE ^ z;
        sieve_side_info_ptr s = si->sides[side];
        /* This serves as a pointer */
        factorbase_degn_t *fb = s->fb;

        /* skip over small primes */
        while (fb->p != FB_END && fb->p < (fbprime_t) si->bucket_thresh)
            fb = fb_next (fb); 
        factorbase_degn_t *fb_bucket[si->nb_threads];
        dispatch_fb(fb_bucket, &s->fb, fb, si->nb_threads, FBPRIME_MAX);
        for (int i = 0; i < si->nb_threads; ++i) {
            thrs[i]->sides[side]->fb_bucket = fb_bucket[i];
        }
        fprintf (si->output, "# Number of small-sieved primes in %s factor base = %zu\n", sidenames[side], fb_nroots_total(s->fb));

        /* Counting the bucket-sieved primes per thread.  */
        unsigned long * nn = (unsigned long *) malloc(si->nb_threads * sizeof(unsigned long));
        ASSERT_ALWAYS(nn);
        memset(nn, 0, si->nb_threads * sizeof(unsigned long));
        for (int i = 0; i < si->nb_threads; ++i) {
            thrs[i]->sides[side]->bucket_fill_ratio = 0;
        }
        for (int i = 0; i < si->nb_threads; ++i) {
            thrs[i]->sides[side]->bucket_fill_ratio = 0;
            fb = thrs[i]->sides[side]->fb_bucket;
            for (; fb->p != FB_END; fb = fb_next(fb)) {
                nn[i] += fb->nr_roots;
                thrs[i]->sides[side]->bucket_fill_ratio += fb->nr_roots / (double) fb->p;
            }
        }
        fprintf (si->output, "# Number of bucket-sieved primes in %s factor base per thread =", sidenames[side]);
        for(int i = 0 ; i < si->nb_threads ; i++)
            fprintf (si->output, " %lu", nn[i]);
        fprintf(si->output, "\n");
        fprintf (si->output, "# Inverse sum of bucket-sieved primes in %s factor base per thread =", sidenames[side]);
        for(int i = 0 ; i < si->nb_threads ; i++)
            fprintf (si->output, " %.5f", thrs[i]->sides[side]->bucket_fill_ratio);
        fprintf(si->output, " [hit jitter %.2f%%]\n",
                100 * (thrs[0]->sides[side]->bucket_fill_ratio / thrs[si->nb_threads-1]->sides[side]->bucket_fill_ratio- 1));
        free(nn);
    }
    return thrs;
}

static void thread_data_free(thread_data * thrs)
{
    sieve_info_ptr si = thrs[0]->si;
    for (int i = 0; i < si->nb_threads; ++i) {
        for(int side = 0 ; side < 2 ; side++) {
            free(thrs[i]->sides[side]->fb_bucket);
        }
    }
    free(thrs); /* nothing to do ! */
}

static void thread_buckets_alloc(thread_data * thrs)
{
    sieve_info_ptr si = thrs[0]->si;
    for (int i = 0; i < si->nb_threads; ++i) {
        thread_data_ptr th = thrs[i];
        for(int side = 0 ; side < 2 ; side++) {
            thread_side_data_ptr ts = th->sides[side];

            int bucket_limit = ts->bucket_fill_ratio * bucket_region;
            bucket_limit *= si->bucket_limit_multiplier;

            ts->BA = init_bucket_array(si->nb_buckets, bucket_limit);

            /*
            double limit_factor =
                log(log(si->cpoly->pols[side]->lim)) -
                log(log(si->bucket_thresh));
            int bucket_limit_base = limit_factor * bucket_region;
            bucket_limit_base *= BUCKET_LIMIT_FACTOR;
            bucket_limit_base /= si->nb_threads;


            fprintf(si->output, "# (thread %d, %s) asymptotic bucket_limit = %d, choosing %d\n", th->id, sidenames[side], bucket_limit_base, bucket_limit);
            */
        }
    }
}

static void thread_buckets_free(thread_data * thrs)
{
    sieve_info_ptr si = thrs[0]->si;
    for(int side = 0 ; side < 2 ; side++) {
        for (int i = 0; i < si->nb_threads; ++i) {
            clear_bucket_array(thrs[i]->sides[side]->BA);
        }
    }
}

static double thread_buckets_max_full(thread_data * thrs)
{
    sieve_info_ptr si = thrs[0]->si;
    double mf, mf0 = 0;
    for (int i = 0; i < si->nb_threads; ++i) {
        mf = buckets_max_full (thrs[i]->sides[0]->BA);
        if (mf > mf0) mf0 = mf;
        mf = buckets_max_full (thrs[i]->sides[1]->BA);
        if (mf > mf0) mf0 = mf;
    }
    return mf0;
}


/*************************** main program ************************************/

static void
usage (const char *argv0, const char * missing)
{
  fprintf (stderr, "Usage: %s [-I I] -poly xxx.poly -fb xxx.roots -q0 q0 [-q1 q1] [-rho rho]\n",
           argv0);
  fprintf (stderr, "          -I i            sieving region has side 2^i [default %u]\n", DEFAULT_I);
  fprintf (stderr, "          -poly xxx.poly  use polynomial xxx.poly\n");
  fprintf (stderr, "          -fb xxx.roots   use factor base xxx.roots\n");
  fprintf (stderr, "          -q0 nnn         left bound of special-q range\n");
  fprintf (stderr, "          -q1 nnn         right bound of special-q range\n");
  fprintf (stderr, "          -rho r          sieve only algebraic root r mod q0\n");
  fprintf (stderr, "          -tdthresh nnn   trial-divide primes p/r <= nnn (r=number of roots)\n");
  fprintf (stderr, "          -bkthresh nnn   bucket-sieve primes p >= nnn\n");
  fprintf (stderr, "          -rlim     nnn   rational factor base bound nnn\n");
  fprintf (stderr, "          -alim     nnn   algebraic factor base bound nnn\n");
  fprintf (stderr, "          -lpbr     nnn   rational large prime bound 2^nnn\n");
  fprintf (stderr, "          -lpba     nnn   algebraic large prime bound 2^nnn\n");
  fprintf (stderr, "          -rat->mfb     nnn   rational cofactor bound 2^nnn\n");
  fprintf (stderr, "          -alg->mfb     nnn   algebraic cofactor bound 2^nnn\n");
  fprintf (stderr, "          -rlambda  nnn   rational lambda value is nnn\n");
  fprintf (stderr, "          -alambda  nnn   algebraic lambda value is nnn\n");
  fprintf (stderr, "          -S        xxx   skewness value is xxx\n");
  fprintf (stderr, "          -v              be verbose (print some sieving statistics)\n");
  fprintf (stderr, "          -out filename   write relations to filename instead of stdout\n");
  fprintf (stderr, "          -mt nnn   use nnn threads\n");
  fprintf (stderr, "          -ratq           use rational special-q\n");
  fprintf (stderr, "          The following are for benchs:\n");
  fprintf (stderr, "          -bench          activate bench mode\n");
  fprintf (stderr, "          -skfact   xxx   skip factor, default=1.01\n");
  fprintf (stderr, "          -bench2         activate alternate bench mode\n");
  fprintf (stderr, "          -percent   xxx  percentage of sieving, default=1e-3\n");
  fprintf (stderr, "          -stats    xxx   write or read statistics file xxx\n");
  fprintf (stderr, "          -stats_prob xxx use threshold xxx\n");
  fprintf (stderr, "          -sievestats xxx write sieve statistics to file xxx\n");
  if (missing) {
      fprintf(stderr, "\nError: missing parameter %s\n", missing);
  }
  exit (EXIT_FAILURE);
}

int
main (int argc0, char *argv0[])
{
    sieve_info si;
    const char *fbfilename = NULL;
    double t0, tfb, tts;
    uint64_t q0 = 0, q1 = 0, rho = 0;
    uint64_t *roots;
    unsigned long nroots;
    int rpow_lim = 0, apow_lim = 0;
    int i;
    unsigned long sq = 0;
    double totJ = 0.0;
    /* following command-line values override those in the polynomial file */
    int argc = argc0;
    char **argv = argv0;
    double max_full = 0.;
    int bench = 0;
    int bench2 = 0;
    double skip_factor = 1.01;  /* next_q = q*skip_factor in bench mode */
    double bench_percent = 1e-3; 
    long bench_tot_rep = 0;
    double bench_tot_time = 0.0;
    const char *statsfilename = NULL;
    const char *sievestatsfilename = NULL;
    int j;

    memset(si, 0, sizeof(sieve_info));

    param_list pl;
    param_list_init(pl);

    param_list_configure_knob(pl, "-v", &si->verbose);
    param_list_configure_knob(pl, "-ratq", &si->ratq);
    param_list_configure_knob(pl, "-bench", &bench);
    param_list_configure_knob(pl, "-bench2", &bench2);
    param_list_configure_alias(pl, "-skew", "-S");

    argv++, argc--;
    for( ; argc ; ) {
        if (param_list_update_cmdline(pl, &argc, &argv)) { continue; }
        /* Could also be a file */
        FILE * f;
        if ((f = fopen(argv[0], "r")) != NULL) {
            param_list_read_stream(pl, f);
            fclose(f);
            argv++,argc--;
            continue;
        }
        fprintf(stderr, "Unhandled parameter %s\n", argv[0]);
        usage(argv0[0],NULL);
    }

    fbfilename = param_list_lookup_string(pl, "fb");
    statsfilename = param_list_lookup_string (pl, "stats");
    sievestatsfilename = param_list_lookup_string (pl, "sievestats");

    param_list_parse_uint64(pl, "q0", &q0);
    param_list_parse_uint64(pl, "q1", &q1);
    param_list_parse_uint64(pl, "rho", &rho);

    param_list_parse_int(pl, "rpowlim", &rpow_lim);
    param_list_parse_int(pl, "apowlim", &apow_lim);
    param_list_parse_double (pl, "stats_prob", &stats_prob);

    // these are parsed in sieve_info_init (why them, and not the above ?)
    // param_list_parse_int(pl, "mt", &nb_threads);
    // param_list_parse_int(pl, "I", &I);

    param_list_parse_double(pl, "skfact", &skip_factor);
    param_list_parse_double(pl, "percent", &bench_percent);

    /* {{{ perform some basic checking */
    if (fbfilename == NULL) usage(argv0[0], "fb");
    if (q0 == 0) usage(argv0[0], "q0");

    /* if -rho is given, we sieve only for q0, thus -q1 is not allowed */
    if (rho != 0 && q1 != 0)
      {
        fprintf (stderr, "Error, -q1 and -rho are mutually exclusive\n");
        exit (EXIT_FAILURE);
      }

    /* if -q1 is not given, sieve only for q0 */
    if (q1 == 0)
      q1 = q0 + 1;

    /* check that q1 fits into an unsigned long */
    if (q1 > (uint64_t) ULONG_MAX)
      {
        fprintf (stderr, "Error, q1=%" PRIu64 " exceeds ULONG_MAX\n", q1);
        exit (EXIT_FAILURE);
      }
    /* }}} */

    /* this does not depend on the special-q */
    sieve_info_init(si, pl);    /* side effects: prints cmdline and flags */

    if (statsfilename != NULL) /* a file was given */
      {
        /* if the file exists, we open it in read-mode, otherwise we create
           it */
        stats_file = fopen (statsfilename, "r");
        if (stats_file != NULL)
          stats = 2;
        else
          {
            stats_file = fopen (statsfilename, "w");
            if (stats_file == NULL)
              {
                fprintf (stderr, "Error, cannot create file %s\n",
                         statsfilename);
                exit (EXIT_FAILURE);
              }
            stats = 1;
          }
      }

    if (sievestatsfilename != NULL) /* a file was given */
      {
        sievestats_file = fopen (sievestatsfilename, "w");
        if (sievestats_file == NULL)
          {
            fprintf (stderr, "Error, cannot create file %s\n",
                     sievestatsfilename);
            exit (EXIT_FAILURE);
          }
      }
    

    /* While obviously, this one does (but only mildly) */
    sieve_info_init_norm_data(si, q0);

    si->bench=bench + bench2;

    sieve_side_info_ptr rat = si->sides[RATIONAL_SIDE];
    sieve_side_info_ptr alg = si->sides[ALGEBRAIC_SIDE];

    /* {{{ Read algebraic factor base */
    {
      fbprime_t *leading_div;
      tfb = seconds ();
      leading_div = factor_small (si->cpoly->alg->f[si->cpoly->alg->degree], si->cpoly->alg->lim);
      alg->fb = fb_read_addproj (fbfilename, alg->scale * LOG_SCALE, 0,
				leading_div);
      ASSERT_ALWAYS(alg->fb != NULL);
      tfb = seconds () - tfb;
      fprintf (si->output, 
               "# Reading algebraic factor base of %zuMb took %1.1fs\n", 
               fb_size (alg->fb) >> 20, tfb);
      free (leading_div);
    }
    /* }}} */
    /* {{{ Prepare rational factor base */
    {
        tfb = seconds ();
        if (rpow_lim >= si->bucket_thresh)
          {
            rpow_lim = si->bucket_thresh - 1;
            printf ("# rpowthresh reduced to %d\n", rpow_lim);
          }
        rat->fb = fb_make_linear ((const mpz_t *) si->cpoly->rat->f, (fbprime_t) si->cpoly->rat->lim,
                                 rpow_lim, rat->scale * LOG_SCALE, 
                                 si->verbose, 1, si->output);
        tfb = seconds () - tfb;
        fprintf (si->output, "# Creating rational factor base of %zuMb took %1.1fs\n",
                 fb_size (rat->fb) >> 20, tfb);
    }
    /* }}} */

    thread_data * thrs = thread_data_alloc(si);

    init_norms (si);

    sieve_info_init_trialdiv(si); /* Init refactoring stuff */
    si->strategy = facul_make_strategy (15, MIN(si->cpoly->rat->lim, si->cpoly->alg->lim),
                                       1UL << MIN(si->cpoly->rat->lpb, si->cpoly->alg->lpb));

    las_report report;
    las_report_init(report);

    /* special q (and root rho) */
    roots = (uint64_t *) malloc (si->cpoly->alg->degree * sizeof (uint64_t));
    ASSERT_ALWAYS(roots);
    q0 --; /* so that nextprime gives q0 if q0 is prime */
    nroots = 0;

    if (stats != 0)
      {
        cof_call = (uint32_t**) malloc ((si->cpoly->rat->mfb + 1) * sizeof(uint32_t*));
        cof_succ = (uint32_t**) malloc ((si->cpoly->rat->mfb + 1) * sizeof(uint32_t*));
        for (i = 0; i <= si->cpoly->rat->mfb; i++)
          {
            cof_call[i] = (uint32_t*) malloc ((si->cpoly->alg->mfb + 1)
                                              * sizeof(uint32_t));
            cof_succ[i] = (uint32_t*) malloc ((si->cpoly->alg->mfb + 1)
                                              * sizeof(uint32_t));
            for (j = 0; j <= si->cpoly->alg->mfb; j++)
              cof_call[i][j] = cof_succ[i][j] = 0;
          }
        if (stats == 2)
          {
            fprintf (si->output,
                    "# Use learning file %s with threshold %1.2e\n",
                     statsfilename, stats_prob);
            while (!feof (stats_file))
              {
                uint32_t c, s;
                if (fscanf (stats_file, "%u %u %u %u\n", &i, &j, &c, &s) != 4)
                  {
                    fprintf (stderr, "Error while reading file %s\n",
                             statsfilename);
                    exit (EXIT_FAILURE);
                  }
                if (i <= si->cpoly->rat->mfb && j <= si->cpoly->alg->mfb)
                  {
                    /* When s=0 and c>0, whatever STATS_PROB, we will always
                       have s/c < STATS_PROB, thus (i,j) will be discarded.
                       We allow a small error by considering (s+1)/(c+1)
                       instead. In case s=0, (i,j) is discarded only when
                       1/(c+1) < STATS_PROB (always discarded for c=0). */
                    cof_call[i][j] = c + 1;
                    cof_succ[i][j] = s + 1;
                  }
              }
          }
      }

    t0 = seconds ();
    fprintf (si->output, "#\n");
    int rep_bench = 0;
    int nbq_bench = 0;
    double t_bench = seconds();

    where_am_I w MAYBE_UNUSED;
    WHERE_AM_I_UPDATE(w, si, si);

    reorder_fb(si, 0);
    reorder_fb(si, 1);

    while (q0 < q1)
      {
        while (nroots == 0) /* {{{ go to next prime and generate roots */
          {
            q0 = uint64_nextprime (q0);
            if (q0 >= q1)
              goto end;  // breaks two whiles.
            si->q = q0;
            if (si->ratq)
                nroots = poly_roots_uint64 (roots, si->cpoly->rat->f, 1, q0);
            else
                nroots = poly_roots_uint64 (roots, si->cpoly->alg->f, si->cpoly->alg->degree, q0);
            if (nroots > 0)
              {
                fprintf (si->output, "### q=%" PRIu64 ": root%s", q0,
                         (nroots == 1) ? "" : "s");
                for (i = 1; i <= (int) nroots; i++)
                  fprintf (si->output, " %" PRIu64, roots[nroots-i]);
                fprintf (si->output, "\n");
              }
          }
        /* }}} */

        /* computes a0, b0, a1, b1 from q, rho, and the skewness */
        si->rho = roots[--nroots];
        if (rho != 0 && si->rho != rho) /* if -rho, wait for wanted root */
          continue;
        if (SkewGauss (si, si->cpoly->skew) != 0)
	  continue;
        /* FIXME: maybe we can discard some special q's if a1/a0 is too large,
           see http://www.mersenneforum.org/showthread.php?p=130478 */

        fprintf (si->output, "# Sieving q=%" PRIu64 "; rho=%" PRIu64
                 "; a0=%d; b0=%d; a1=%d; b1=%d\n",
                 si->q, si->rho, si->a0, si->b0, si->a1, si->b1);
        sq ++;

        /* checks the value of J,
         * precompute the skewed polynomials of f(x) and g(x), and also
         * their floating-point versions */
        sieve_info_update (si);
        totJ += (double) si->J;

            trace_update_conditions(si);

            report->ttsm -= seconds();

            /* Allocate buckets */
            thread_buckets_alloc(thrs);

            /* Fill in rat and alg buckets */
            thread_do(thrs, &fill_in_buckets_both);

            max_full = thread_buckets_max_full(thrs);
            if (max_full >= 1.0) {
                fprintf(stderr, "maxfull=%f\n", max_full);
                for (i = 0; i < si->nb_threads; ++i) {
                    fprintf(stderr, "intend to free [%d] max_full=%f %f\n",
                            i,
                            buckets_max_full (thrs[i]->sides[0]->BA),
                            buckets_max_full (thrs[i]->sides[1]->BA));
                }
                thread_buckets_free(thrs); /* may crash. See below */

                si->bucket_limit_multiplier *= 1.1 * max_full;
                max_full = 1.0/1.1;
                nroots++;   // ugly: redo the same class
                // when doing one big malloc, there's some chance that the
                // bucket overrun actually stepped over the next bucket. In
                // this case, the freeing of buckets in the code above might
                // have succeeded, so we can hope to resume with this special
                // q. On the other hand, if we have one malloc per bucket,
                // the free() calls above are guaranteed to crash.
                // Thus it's okay to proceed, if we're lucky enough to reach
                // here. Note that increasing bucket_limit will have a
                // permanent effect on the rest of this run.
                // abort();
                continue;
            }

            report->ttsm += seconds();

            /* Process bucket regions in parallel */
            thread_do(thrs, &process_bucket_region);

            /* Display results for this special q */
            {
                las_report rep;
                las_report_init(rep);
                for (int i = 0; i < si->nb_threads; ++i) {
                    las_report_accumulate(rep, thrs[i]->rep);
                }
                if (si->verbose) {
                    fprintf (si->output, "# %lu survivors after rational sieve,", rep->survivors0);
                    fprintf (si->output, " %lu survivors after algebraic sieve, ", rep->survivors1);
                    fprintf (si->output, "coprime: %lu\n", rep->survivors2);
                }
                fprintf (si->output, "# %lu relation(s) for (%" PRIu64 ",%" PRIu64")\n", rep->reports, si->q, si->rho);
                rep_bench += rep->reports;
                las_report_accumulate(report, rep);
                las_report_clear(rep);
            }
            
            thread_buckets_free(thrs);

        /* {{{ bench stats */
        if (bench) {
            uint64_t newq0 = (uint64_t) (skip_factor*((double) q0));
            uint64_t savq0 = q0;
            // print some estimates for special-q's between q0 and the next
            int nb_q = 1;
            do {
                q0 = uint64_nextprime (q0);
                nb_q ++;
            } while (q0 < newq0);
            q0 = newq0;
            nroots=0;
            t_bench = seconds() - t_bench;
            fprintf(si->output,
              "# Stats for q=%" PRIu64 ": %d reports in %1.1f s\n",
              savq0, rep_bench, t0);
            fprintf(si->output,
              "# Estimates for next %d q's: %d reports in %1.0f s, %1.2f s/r\n",
              nb_q, nb_q*rep_bench, t0*nb_q, t0/((double)rep_bench));
            bench_tot_time += t0*nb_q;
            bench_tot_rep += nb_q*rep_bench;
            rep_bench = 0;
            fprintf(si->output, "# Cumulative (estimated): %lu reports in %1.0f s, %1.2f s/r\n",
                    bench_tot_rep, bench_tot_time,
		    (double) bench_tot_time / (double) bench_tot_rep);
            t_bench = seconds();
        }
        /* }}} */
        /* {{{ bench stats */
        if (bench2) {
            nbq_bench++;
            const int BENCH2 = 50;
            if (rep_bench >= BENCH2) {
                t_bench = seconds() - t_bench;
                fprintf(si->output,
                  "# Got %d reports in %1.1f s using %d specialQ\n",
                  rep_bench, t_bench, nbq_bench);
                double relperq = (double)rep_bench / (double)nbq_bench;
                double est_rep = (double)rep_bench;
                do {
                    q0 = uint64_nextprime (q0);
                    est_rep += relperq;
                } while (est_rep <= BENCH2 / bench_percent);
                fprintf(si->output,
                  "# Extrapolate to %ld reports up to q = %" PRIu64 "\n",
                  (long) est_rep, q0);
                bench_tot_time += t_bench / bench_percent;
                bench_tot_rep += BENCH2 / bench_percent;
                fprintf(si->output,
                  "# Cumulative (estimated): %lu reports in %1.0f s, %1.2f s/r\n",
                  bench_tot_rep, bench_tot_time,
                  (double) bench_tot_time / (double) bench_tot_rep);
                // reinit for next slice of bench:
                t_bench = seconds();
                nbq_bench = 0;
                rep_bench = 0;
                nroots=0;
            }
        }
        /* }}} */
      } // end of loop over special q ideals.

 end:
    /* {{{ stats */
    t0 = seconds () - t0;
    fprintf (si->output, "# Average J=%1.0f for %lu special-q's, max bucket fill %f\n",
             totJ / (double) sq, sq, max_full);
    tts = t0;
    tts -= report->tn[0];
    tts -= report->tn[1];
    tts -= report->ttf;
    if (si->verbose)
      facul_print_stats (si->output);
    if (sievestats_file != NULL)
    {
        fprintf (sievestats_file, "# Number of sieve survivors and relations by sieve residue pair\n");
        fprintf (sievestats_file, "# Format: S1 S2 #relations #survivors ratio\n");
        fprintf (sievestats_file, "# where S1 is the sieve residue on the rational side, S2 rational side\n");
        fprintf (sievestats_file, "# Make a pretty graph with gnuplot:\n");
        fprintf (sievestats_file, "# splot \"sievestatsfile\" using 1:2:3 with pm3d\n");
        fprintf (sievestats_file, "# plots histogram for relations, 1:2:4 for survivors, 1:2:($3/$4) for ratio\n");
        for(int i1 = 0 ; i1 < 256 ; i1++) {
            for (int i2 = 0; i2 < 256; i2++) {
                unsigned long r1 = report->report_sizes[i1][i2];
                unsigned long r2 = report->survivor_sizes[i1][i2];
                if (r1 > r2) {
                  fprintf(stderr, "Error, statistics report more relations (%lu) than "
                          "sieve survivors (%lu) for (%d,%d)\n", r1, r2, i1, i2)
;
                }
                if (r2 > 0)
                    fprintf (sievestats_file, "%d %d %lu %lu\n", 
                             i1, i2, r1, r2);
            }
            fprintf (sievestats_file, "\n");
        }
        fclose(sievestats_file);
        sievestats_file = NULL;
    }
    if (si->nb_threads > 1) 
        fprintf (si->output, "# Total wct time %1.1fs [precise timings available only for mono-thread]\n", t0);
    else
        fprintf (si->output, "# Total time %1.1fs [norm %1.2f+%1.1f, sieving %1.1f"
            " (%1.1f + %1.1f),"
             " factor %1.1f]\n", t0,
             report->tn[RATIONAL_SIDE],
             report->tn[ALGEBRAIC_SIDE],
             tts, report->ttsm, tts-report->ttsm, report->ttf);
    fprintf (si->output, "# Total %lu reports [%1.3fs/r, %1.1fr/sq]\n",
             report->reports, t0 / (double) report->reports,
             (double) report->reports / (double) sq);
    if (bench || bench2) {
        fprintf(si->output, "# Total (estimated): %lu reports in %1.1f s\n",
                bench_tot_rep, bench_tot_time);
    }
    /* }}} */

    /* {{{ stats */
    if (bucket_prime_stats) 
      {
        printf ("# Number of bucket primes: %ld\n", nr_bucket_primes);
        printf ("# Number of divisibility tests of bucket primes: %ld\n", 
                nr_div_tests);
        printf ("# Number of compositeness tests of bucket primes: %ld\n", 
                nr_composite_tests);
        printf ("# Number of wrapped composite values while dividing out "
                "bucket primes: %ld\n", nr_wrap_was_composite);
      }
    if (stats == 2)
      fprintf (si->output, "# Rejected %u cofactorizations out of %u due to stats file\n", cof_call[0][0] - cof_succ[0][0], cof_call[0][0]);
    /* }}} */

    sieve_info_clear_trialdiv(si);
    sieve_info_clear_norm_data(si);

    facul_clear_strategy (si->strategy);
    si->strategy = NULL;

    thread_data_free(thrs);

    free(si->sides[0]->fb);
    free(si->sides[1]->fb);
    free (roots);
    las_report_clear(report);

    sieve_info_clear (si);

    param_list_clear(pl);

    if (stats != 0)
      {
        for (i = 0; i <= si->cpoly->rat->mfb; i++)
          {
            if (stats == 1)
              for (j = 0; j <= si->cpoly->alg->mfb; j++)
                fprintf (stats_file, "%u %u %u %u\n", i, j, cof_call[i][j],
                         cof_succ[i][j]);
            free (cof_call[i]);
            free (cof_succ[i]);
          }
        free (cof_call);
        free (cof_succ);
        fclose (stats_file);
      }

    return 0;
}
