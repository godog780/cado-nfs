#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "utils.h"
#include "prac_bc.h"
#include "ecm.h"

typedef struct {residue_t x, z;} __ellM_point_t;
typedef __ellM_point_t ellM_point_t[1];

static inline void
ellM_init (ellM_point_t P, const modulus_t m)
{
  mod_init (P->x, m);
  mod_init (P->z, m);
}

static inline void
ellM_clear (ellM_point_t P, const modulus_t m)
{
  mod_clear (P->x, m);
  mod_clear (P->z, m);
}

static inline void
ellM_set (ellM_point_t Q, const ellM_point_t P, const modulus_t m)
{
  mod_set (Q->x, P->x, m);
  mod_set (Q->z, P->z, m);
}

static inline void
ellM_swap (ellM_point_t Q, ellM_point_t P, const modulus_t m)
{
  mod_swap (Q->x, P->x, m);
  mod_swap (Q->z, P->z, m);
}

/* computes Q=2P, with 5 muls (3 muls and 2 squares) and 4 add/sub.
     - m : number to factor
     - b : (a+2)/4 mod n
  It is permissible to let x1, z1 and x2, z2 use the same memory. */

static void
ellM_double (ellM_point_t Q, const ellM_point_t P, const modulus_t m, 
             const residue_t b)
{
  residue_t u, v, w;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (w, m);

  mod_add (u, P->x, P->z, m);
  mod_mul (u, u, u, m);   /* u = (x1 + z1)^2 */
  mod_sub (v, P->x, P->z, m);
  mod_mul (v, v, v, m);   /* v = (x1 - z1)^2 */
  mod_mul (Q->x, u, v, m);  /* x2 = (x1^2 - z1^2)^2 */
  mod_sub (w, u, v, m);   /* w = 4 * x1 * z1 */
  mod_mul (u, w, b, m);   /* u = x1 * z1 * (A + 2) */
  mod_add (u, u, v, m);
  mod_mul (Q->z, w, u, m);

  mod_clear (w, m);
  mod_clear (v, m);
  mod_clear (u, m);
}


/* For Weierstrass coordinates. Returns 1 if doubling worked normally, 
   0 if the result is point at infinity */

static int
ellW_double (residue_t x3, residue_t y3, residue_t x1, residue_t y1,
	     residue_t a, const modulus_t m)
{
  residue_t lambda, u, v;

  mod_init_noset0 (lambda, m);
  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);

  mod_mul (u, x1, x1, m);
  mod_add (v, u, u, m);
  mod_add (v, v, u, m);
  mod_add (v, v, a, m); /* 3x^2 + a */
  mod_add (u, y1, y1, m);
  if (mod_inv (u, u, m) == 0)    /* 1/(2*y) */
  {
      mod_clear (v, m);
      mod_clear (u, m);
      mod_clear (lambda, m);
      return 0; /* y was 0  =>  result is point at infinity */
  }
  mod_mul (lambda, u, v, m);
  mod_mul (u, lambda, lambda, m);
  mod_sub (u, u, x1, m);
  mod_sub (u, u, x1, m);    /* x3 = u = lambda^2 - 2*x */
  mod_sub (v, x1, u, m);
  mod_mul (v, v, lambda, m);
  mod_sub (y3, v, y1, m);
  mod_set (x3, u, m);
  
  mod_clear (v, m);
  mod_clear (u, m);
  mod_clear (lambda, m);
  return 1;
}


/* adds P and Q and puts the result in R,
     using 6 muls (4 muls and 2 squares), and 6 add/sub.
   One assumes that Q-R=D or R-Q=D.
   This function assumes that P !~= Q, i.e. that there is 
   no t!=0 so that P->x = t*Q->x and P->z = t*Q->z, for otherwise the result 
   is the Not-a-Point (0:0) (which actually is good for factoring!).

   R may be identical to P, Q and/or D. */

static void
ellM_add (ellM_point_t R, const ellM_point_t P, const ellM_point_t Q, 
          const ellM_point_t D, const modulus_t m)
{
  residue_t u, v, w;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (w, m);

  mod_sub (u, P->x, P->z, m);
  mod_add (v, Q->x, Q->z, m);
  mod_mul (u, u, v, m);
  mod_add (w, P->x, P->z, m);
  mod_sub (v, Q->x, Q->z, m);
  mod_mul (v, w, v, m);
  mod_add (w, u, v, m);
  mod_sub (v, u, v, m);
  mod_mul (w, w, w, m);
  mod_mul (v, v, v, m);
  mod_set (u, D->x, m); /* save D->x */
  mod_mul (R->x, w, D->z, m);
  mod_mul (R->z, u, v, m);

  mod_clear (w, m);
  mod_clear (v, m);
  mod_clear (u, m);
}


/* Adds two points (x2, y2) and (x1, y1) on the curve y^2 = x^3 + a*x + b
   in Weierstrass coordinates and puts result in (x3, y3). 
   Returns 1 if the addition worked (i.e. the modular inverse existed) 
   and 0 otherwise (resulting point is point at infinity) */

static int
ellW_add3 (residue_t x3, residue_t y3, residue_t x2, residue_t y2, 
           residue_t x1, residue_t y1, residue_t a, const modulus_t m)
{
  residue_t lambda, u, v;
  int r;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);

  mod_sub (u, y2, y1, m);
  mod_sub (v, x2, x1, m);
  r = mod_inv (v, v, m);
  if (r == 0)
  {
      /* Maybe we were trying to add two identical points? If so,
         use the ellW_double() function instead */
      if (mod_equal (x1, x2, m) && mod_equal (y1, y2, m))
	  r = ellW_double (x3, y3, x1, y1, a, m);
      else
	  r = 0; /* No, the points were negatives of each other */
  }
  else
  {
      mod_mul (lambda, u, v, m);
      mod_mul (u, lambda, lambda, m);
      mod_sub (u, u, x1, m);
      mod_sub (u, u, x2, m);    /* x3 = u = lambda^2 - x1 - x2 */
      mod_sub (v, x1, u, m);
      mod_mul (v, v, lambda, m);
      mod_sub (y3, v, y1, m);
      mod_set (x3, u, m);
      r = 1;
  }

  mod_clear (v, m);
  mod_clear (u, m);
  return r;
}


/* (x:z) <- e*(x:z) (mod p)
   Assumes e >= 5.
*/
static void
ellM_mul_ui (ellM_point_t P, unsigned long e, const modulus_t m, 
             const residue_t b)
{
  unsigned long l, n;
  ellM_point_t t1, t2;

  ASSERT (e >= 5UL);

  ellM_init (t1, m);
  ellM_init (t2, m);

  e --;

  /* compute number of steps needed: we start from (1,2) and go from
     (i,i+1) to (2i,2i+1) or (2i+1,2i+2) */
  for (l = e, n = 0; l > 1; n ++, l /= 2);

  /* start from P1=P, P2=2P */
  ellM_set (t1, P, m);
  ellM_double (t2, t1, m, b);

  while (n--)
    {
      if ((e >> n) & 1) /* (i,i+1) -> (2i+1,2i+2) */
        {
          /* printf ("(i,i+1) -> (2i+1,2i+2)\n"); */
          ellM_add (t1, t1, t2, P, m);
          ellM_double (t2, t2, m, b);
        }
      else /* (i,i+1) -> (2i,2i+1) */
        {
          /* printf ("(i,i+1) -> (2i,2i+1)\n"); */
          ellM_add (t2, t1, t2, P, m);
          ellM_double (t1, t1, m, b);
        }
    }
  
  ellM_set (P, t2, m);

  ellM_clear (t1, m);
  ellM_clear (t2, m);
}

static int
ellW_mul_ui (residue_t x, residue_t y, const unsigned long e, residue_t a, 
	     const modulus_t m)
{
  unsigned long i;
  residue_t xt, yt;
  int tfinite; /* Nonzero iff (xt, yt) is NOT point at infinity */

  if (e == 0)
    return 0; /* signal point at infinity */

  mod_init_noset0 (xt, m);
  mod_init_noset0 (yt, m);

  i = ~(0UL);
  i -= i/2;   /* Now the most significant bit of i is set */
  while ((i & e) == 0)
    i >>= 1;

  mod_set (xt, x, m);
  mod_set (yt, y, m);
  tfinite = 1;
  i >>= 1;

  while (i > 0)
  {
      if (tfinite)
        tfinite = ellW_double (xt, yt, xt, yt, a, m);
      if (e & i)
      {
	  if (tfinite)
	      tfinite = ellW_add3 (xt, yt, x, y, xt, yt, a, m);
	  else
	  {
	      mod_set (xt, x, m);
	      mod_set (yt, y, m);
	      tfinite = 1;
	  }
      }
      i >>= 1;
  }

  if (tfinite)
  {
      mod_set (x, xt, m);
      mod_set (y, yt, m);
  }
  mod_clear (yt, m);
  mod_clear (xt, m);

  return tfinite;
}


/* Interpret the "l" bytes of bytecode located at "code" and do the 
   corresponding elliptic curve operations on (x::z) */

static void
ellM_interpret_bytecode (ellM_point_t P, const char *code,
			 const unsigned long l, const modulus_t m, 
			 const residue_t b)
{
  unsigned long i;
  ellM_point_t A, B, C, t, t2;
  
  ellM_init (A, m);
  ellM_init (B, m);
  ellM_init (C, m);
  ellM_init (t, m);
  ellM_init (t2, m);

  ellM_set (A, P, m);

  for (i = 0; i < l; i++)
    {
      switch (code[i])
        {
          case 10: /* Init of subchain, B=A, C=A, A=2*A */
            ellM_set (B, A, m);
            ellM_set (C, A, m);
            ellM_double (A, A, m, b);
            break;
          case 0: /* Swap A, B */
            ellM_swap (A, B, m);
            break;
          case 1:
            ellM_add (t, A, B, C, m);
            ellM_add (t2, t, A, B, m);
            ellM_add (B, B, t, A, m);
            ellM_set (A, t2, m);
            break;
          case 2:
            ellM_add (B, A, B, C, m);
            ellM_double (A, A, m, b);
            break;
          case 3:
            ellM_add (C, B, A, C, m);
            ellM_swap (B, C, m);
            break;
          case 4:
            ellM_add (B, B, A, C, m);
            ellM_double (A, A, m, b);
            break;
          case 5:
            ellM_add (C, C, A, B, m);
            ellM_double (A, A, m, b);
            break;
          case 6:
            ellM_double (t, A, m, b);
            ellM_add (t2, A, B, C, m);
            ellM_add (A, t, A, A, m);
            ellM_add (C, t, t2, C, m);
            ellM_swap (B, C, m);
            break;
          case 7:
            ellM_add (t, A, B, C, m);
            ellM_add (B, t, A, B, m);
            ellM_double (t, A, m, b);
            ellM_add (A, A, t, A, m);
            break;
          case 8:
            ellM_add (t, A, B, C, m);
            ellM_add (C, C, A, B, m);
            ellM_swap (B, t, m);
            ellM_double (t, A, m, b);
            ellM_add (A, A, t, A, m);
            break;
          case 9:
            ellM_add (C, C, B, A, m);
            ellM_double (B, B, m, b);
            break;
          case 11:
            ellM_add (A, A, B, C, m); /* Final add */
            break;
          case 12:
            ellM_double (A, A, m, b); /* For p=2 */
            break;
          default:
            abort ();
        }
    }

  ellM_set (P, A, m);

  ellM_clear (A, m);
  ellM_clear (B, m);
  ellM_clear (C, m);
  ellM_clear (t, m);
  ellM_clear (t2, m);
}


/* Produces curve in Montgomery form from sigma value.
   Return 1 if it worked, 0 if a modular inverse failed */

static int
Brent12_curve_from_sigma (residue_t A, residue_t x, const residue_t sigma, 
			  const modulus_t m)
{
  residue_t u, v, t, b, z;
  int r;

  mod_init_noset0 (u, m);
  mod_init_noset0 (v, m);
  mod_init_noset0 (t, m);
  mod_init_noset0 (b, m);
  mod_init_noset0 (z, m);

  /* compute b, x */
  mod_add (v, sigma, sigma, m);
  mod_add (v, v, v, m); /* v = 4*sigma */
  mod_mul (u, sigma, sigma, m);
  mod_set_ul (t, 5UL, m);
  mod_sub (u, u, t, m); /* u = sigma^2 - 5 */
  mod_mul (t, u, u, m);
  mod_mul (x, t, u, m);
  mod_mul (t, v, v, m);
  mod_mul (z, t, v, m);
  mod_mul (t, x, v, m);
  mod_add (b, t, t, m);
  mod_add (b, b, b, m); /* b = 4 * t */
  mod_add (t, u, u, m);
  mod_add (t, t, u, m); /* t = 3 * u */
  mod_sub (u, v, u, m);
  mod_add (v, t, v, m);
  mod_mul (t, u, u, m);
  mod_mul (u, t, u, m);
  mod_mul (A, u, v, m);
  mod_mul (v, b, z, m);

  r = mod_inv (u, v, m);
  if (r) /* non trivial gcd */
  {
      mod_mul (v, u, b, m);
      mod_mul (x, x, v, m);
      mod_mul (v, u, z, m);
      mod_mul (t, A, v, m);
      mod_set_ul (u, 2UL, m);
      mod_sub (A, t, u, m);
  }

  mod_clear (z, m);
  mod_clear (b, m);
  mod_clear (t, m);
  mod_clear (v, m);
  mod_clear (u, m);

  return r;
}

/* Produces curve in Montgomery parameterization from n value, using
   parameters for a torsion 12 curve as in Montgomery's thesis.
   Return 1 if it worked, 0 if a modular inverse failed */

static int
Monty12_curve_from_k (residue_t A, residue_t x, unsigned long n, 
		      const modulus_t m)
{
  residue_t u, v, u0, v0, a, t2;
  
  /* We want a multiple of the point (-2,4) on the curve Y^2=X^3-12*X */
  mod_init (a, m);
  mod_init (u, m);
  mod_init_noset0 (v, m);
  mod_init (u0, m);
  mod_init (v0, m);

  mod_sub_ul (a, a, 12UL, m);
  mod_sub_ul (u, u, 2UL, m);
  mod_set_ul (v, 4UL, m);
  ellW_mul_ui (u, v, n/2, a, m);
  if (n % 2 == 1)
    ellW_add3 (u, v, u, v, u0, v0, a, m);
  /* Now we have a $u$ so that $u^3-12u$ is a square */
  mod_clear (u0, m);
  mod_clear (v0, m);
  /* printf ("Monty12_curve_from_k: u = %lu\n", mod_get_ul (u)); */
  
  mod_init_noset0 (t2, m);
  mod_div2 (v, u, m);
  mod_mul (t2, v, v, m); /* u^2/4 */
  mod_sub_ul (t2, t2, 3UL, m);
  if (mod_inv (u, u, m) == 0)
  {
    fprintf (stderr, "Monty12_curve_from_k: u = 0\n");
    mod_clear (t2, m);
    mod_clear (v, m);
    mod_clear (u, m);
    mod_clear (a, m);
    return 0;
  }
  mod_mul (t2, t2, u, m); /* t^2 = (u^2/4 - 3)/u = (u^2 - 12)/4u */

  mod_sub_ul (u, t2, 1UL, m);
  mod_add_ul (v, t2, 3UL, m);
  mod_mul (a, u, v, m);
  if (mod_inv (a, a, m) == 0) /* a  = 1/(uv), I want u/v and v/u */
  {
    fprintf (stderr, "Monty12_curve_from_k: (t^2 - 1)(t^2 + 3) = 0\n");
    mod_clear (t2, m);
    mod_clear (v, m);
    mod_clear (u, m);
    mod_clear (a, m);
    return 0;
  }
  mod_mul (u, u, u, m); /* u^2 */
  mod_mul (v, v, v, m); /* v^2 */
  mod_mul (v, v, a, m); /* v^2 * (1/(uv)) = v/u = 1/a*/
  mod_mul (a, a, u, m); /* u^2 * (1/(uv)) = u/v = a*/

  mod_mul (u, a, a, m); /* a^2 */
  mod_add_ul (A, u, 2UL, m); /* a^2 + 2 */
  mod_add (t2, A, A, m);
  mod_add (A, A, t2, m); /* 3*(a^2 + 2) */
  mod_mul (t2, A, a, m);
  mod_set (A, v, m);
  mod_sub (A, A, t2, m); /* 1/a - 3 a (a^2 + 2) */
  mod_div2 (v, v, m); /* v = 1/(2a) */
  mod_mul (t2, v, v, m); /* t2 = 1/(2a)^2 */
  mod_mul (A, A, t2, m);

  mod_add (x, u, u, m);
  mod_add (x, x, u, m); /* 3*a^2 */
  mod_add_ul (x, x, 1UL, m); /* 3*a^2 + 1 */
  mod_div2 (v, v, m); /* v = 1/(4a) */
  mod_mul (x, x, v, m);
  
  mod_clear (t2, m);
  mod_clear (v, m);
  mod_clear (u, m);
  mod_clear (a, m);
  return 1;
}


/* Make a curve of the form y^2 = x^3 + a*x^2 + b with a valid point
   (x, y) from a curve Y^2 = X^3 + A*X^2 + X. The value of b will not
   be computed. 
   x and X may be the same variable. */

static int
curveW_from_Montgomery (residue_t a, residue_t x, residue_t y,
			residue_t X, residue_t A, const modulus_t m)
{
  residue_t g, one;
  int r;

  mod_init_noset0 (g, m);
  mod_init_noset0 (one, m);

  mod_set_ul (one, 1UL, m);
  mod_add (g, X, A, m);
  mod_mul (g, g, X, m);
  mod_add_ul (g, g, 1UL, m);
  mod_mul (g, g, X, m); /* G = X^3 + A*X^2 + X */
  /* printf ("curveW_from_Montgomery: Y^2 = %lu\n", g[0]); */

  /* Now (x,1) is on the curve G*Y^2 = X^3 + A*X^2 + X. */
  r = mod_inv (g, g, m);
  if (r != 0)
  {
      mod_set (y, g, m);       /* y = 1/G */
      mod_div3 (a, A, m);
      mod_add (x, X, a, m);
      mod_mul (x, x, g, m); /* x = (X + A/3)/G */
      mod_mul (a, a, A, m);
      mod_sub (a, one, a, m);
      mod_mul (a, a, g, m);
      mod_mul (a, a, g, m); /* a = (1 - (A^2)/3)/G^2 */
  }
  else
    fprintf (stderr, "curveW_from_Montgomery: r = 0\n");

  mod_clear (one, m);
  mod_clear (g, m);

  return r;
}


/* If a factor is found it is returned and x1 is unchanged, otherwise 
   1 is returned and the end-of-stage-1 residue is stored in x1. */

unsigned long
ecm (residue_t x1, const modulus_t m, const ecm_plan_t *plan)
{
  residue_t u, A, b;
  ellM_point_t P, Pt;
  unsigned long f = 1;

  mod_init (u, m);
  mod_init (A, m);
  mod_init (b, m);
  ellM_init (P, m);
  ellM_init (Pt, m);

  if (plan->parameterization == BRENT12)
  {
    residue_t s;
    mod_init_noset0 (s, m);
    mod_set_ul (s, plan->sigma, m);
    if (Brent12_curve_from_sigma (A, P->x, s, m) == 0)
      {
	mod_clear (u, m);
	mod_clear (A, m);
	mod_clear (b, m);
	ellM_clear (P, m);
	ellM_clear (Pt, m);
	mod_clear (s, m);
	return 1;
      }
    mod_clear (s, m);
    mod_set_ul (P->z, 1UL, m);
  }
  else if (plan->parameterization == MONTY12)
  {
    if (Monty12_curve_from_k (A, P->x, plan->sigma, m) == 0)
      return 1;
    mod_set_ul (P->z, 1UL, m);
  }
  else
  {
    fprintf (stderr, "ecm: Unknown parameterization\n");
    abort();
  }

#ifdef TRACE
  printf ("starting point: (%lu::%lu)\n", 
	  mod_get_ul (P->x, m), mod_get_ul (P->z, m));
#endif

  mod_add_ul (b, A, 2UL, m);
  mod_div2 (b, b, m);
  mod_div2 (b, b, m);

  /* now start ecm */

  /* Do stage 1 */
  ellM_interpret_bytecode (P, plan->bc, plan->bc_len, m, b);

  if (!mod_inv (u, P->z, m))
    mod_gcd (&f, P->z, m);
  else
    mod_mul (x1, P->x, u, m); /* No factor. Set x1 to normalized point */
  
  mod_clear (u, m);
  mod_clear (A, m);
  mod_clear (b, m);
  ellM_clear (P, m);
  ellM_clear (Pt, m);

  return f;
}


/* Make byte code for addition chain for stage 1, and the parameters for 
   stage 2 */

void 
ecm_make_plan (ecm_plan_t *plan, const unsigned int B1, const unsigned int B2,
	       const int parameterization, const unsigned long sigma, 
	       const int verbose)
{
  unsigned int p;
  const unsigned int addcost = 6, doublecost = 5; /* TODO: find good ratio */
  const unsigned int compress = 0;
  
  /* Make bytecode for stage 1 */
  plan->B1 = B1;
  plan->parameterization = parameterization;
  plan->sigma = sigma;
  bytecoder_init (compress);
  for (p = 2; p <= B1; p = (unsigned int) getprime (p))
    {
      unsigned long q;
      for (q = p; q <= B1; q *= p)
	prac_bytecode (p, addcost, doublecost);
    }
  bytecoder_flush ();
  plan->bc_len = bytecoder_size ();
  plan->bc = (char *) malloc (plan->bc_len);
  ASSERT (plan->bc);
  bytecoder_read (plan->bc);
  bytecoder_clear ();
  getprime (0);

  if (verbose)
    {
      printf ("Byte code for stage 1 (length %d): ", plan->bc_len);
      for (p = 0; p < plan->bc_len; p++)
	printf ("%s%d", (p == 0) ? "" : ", ", (int) (plan->bc[p]));
      printf ("\n");
    }
    
  /* Make stage 2 plan */
  stage2_make_plan (&(plan->stage2), B1, B2, verbose);
}

void 
ecm_clear_plan (ecm_plan_t *plan)
{
  stage2_clear_plan (&(plan->stage2));
  free (plan->bc);
  plan->bc = NULL;
  plan->bc_len = 0;
  plan->B1 = 0;
}


/* Determine order of a point P on a curve, both defined by the sigma value
   as in ECM. Looks for i in Hasse interval so that i*P = O, has complexity
   O(sqrt(m)). */

unsigned long
ell_pointorder (const residue_t sigma, const int parameterization, 
		const modulus_t m, const int verbose)
{
  residue_t A, x, a, xi, yi, x1, y1;
  unsigned long min, max, i, order, p;

  mod_init (A, m);

  if (parameterization == BRENT12)
    {
      if (Brent12_curve_from_sigma (A, x, sigma, m) == 0)
	return 0;
    }
  else if (parameterization == MONTY12)
  {
    if (Monty12_curve_from_k (A, x, mod_get_ul (sigma, m), m) == 0)
      return 1;
  }
  else
  {
    fprintf (stderr, "ecm: Unknown parameterization\n");
    abort();
  }
  
  if (verbose >= 2)
    printf ("Curve parameters: A = %ld, x = %ld (mod %ld)\n", 
            mod_get_ul (A, m), mod_get_ul (x, m), mod_getmod_ul (m));

  if (curveW_from_Montgomery (a, x1, y1, x, A, m) == 0)
    return 0UL;

  if (verbose >= 2)
    printf ("Finding order of point (%ld, %ld) on curve "
	    "y^2 = x^3 + %ld * x + b (mod %ld)\n", 
	    mod_get_ul (x1, m), mod_get_ul (y1, m), mod_get_ul (a, m), 
	    mod_getmod_ul (m));

  i = 2 * (unsigned long) sqrt((double) mod_getmod_ul (m));
  min = mod_getmod_ul (m) - i + 1;
  max = mod_getmod_ul (m) + i + 1;
  mod_set (xi, x1, m);
  mod_set (yi, y1, m);
  if (ellW_mul_ui (xi, yi, min, a, m) == 0)
  {
      i = min;
  }
  else
  {
      for (i = min + 1; i <= max; i++)
      {
	  if (!ellW_add3 (xi, yi, xi, yi, x1, y1, a, m))
	      break;
      }
      
      if (i > max)
      {
	  fprintf (stderr, "ell_order: Error, point at infinity not "
		   "reached with i*(x0, z0), i in [%ld, %ld]\n", min, max);
	  return 0UL;
      }

      /* Check that this is the correct order */
      mod_set (xi, x1, m);
      mod_set (yi, y1, m);
      if (ellW_mul_ui (xi, yi, i, a, m) != 0)
      {
	  fprintf (stderr, "ell_order: Error, %ld*(%ld, %ld) (mod %ld) is "
		   "not the point at infinity\n", 
		   i, mod_get_ul (x1, m), mod_get_ul (y1, m), 
		   mod_getmod_ul (m));
	  return 0UL;
      }
  }
  
  /* Ok, now we have some i so that ord(P) | i. Find ord(P).
     We know that ord(P) > 1 since P is not at infinity */

  order = i;
  for (p = 2; p * p <= order; p++)
  {
      mod_set (xi, x1, m);
      mod_set (yi, y1, m);
      while (order % p == 0 && ellW_mul_ui (xi, yi, order / p, a, m) == 0)
	  order /= p;
  }

  return order;
}


/* Count points on curve using the Jacobi symbol. This has complexity O(m). */

unsigned long 
ellM_curveorderjacobi (residue_t A, residue_t x, modulus_t m)
{
  residue_t t;
  unsigned long order, i;
  int bchar;

  mod_init_noset0 (t, m);

  /* Compute x^3 + A*x^2 + x and see if it is a square */
  mod_set (t, x, m);
  mod_add (t, t, A, m);
  mod_mul (t, t, x, m);
  mod_add_ul (t, t, 1UL, m);
  mod_mul (t, t, x, m);
  bchar = mod_jacobi (t, m);
  ASSERT (bchar != 0);

  order = 2; /* One for (0, 0, 1), one for the point at infinity */
  for (i = 1; i < mod_getmod_ul(m); i++)
    {
      mod_set_ul (x, i, m);
      mod_set (t, x, m);
      mod_add (t, t, A, m);
      mod_mul (t, t, x, m);
      mod_add_ul (t, t, 1UL, m);
      mod_mul (t, t, x, m);
      if (bchar == 1) 
	order = order + (unsigned long) (1L + (long) mod_jacobi (t, m));
      else
	order = order + (unsigned long) (1L - (long) mod_jacobi (t, m));
	/* Brackets put like this to avoid signedness warning */
    }

  mod_clear (t, m);
  
  return order;
}

unsigned long 
ell_curveorder (const unsigned long sigma_par, int parameterization, 
		const unsigned long m_par)
{
  residue_t sigma, A, X;
  modulus_t m;
  unsigned long order;

  mod_initmod_ul (m, m_par);
  mod_set_ul (sigma, sigma_par, m);

  if (parameterization == BRENT12)
  {
    if (Brent12_curve_from_sigma (A, X, sigma, m) == 0)
      return 0UL;
  }
  else if (parameterization == MONTY12)
  {
    if (Monty12_curve_from_k (A, X, sigma_par, m) == 0)
      return 0UL;
  }
  else
  {
    fprintf (stderr, "ell_curveorder: Unknown parameterization\n");
    abort();
  }
  order = ellM_curveorderjacobi (A, X, m);

#ifndef NDEBUG
  ASSERT (parameterization != BRENT12 || order == ell_pointorder (sigma, parameterization, m, 0));
#endif

  return order;
}
