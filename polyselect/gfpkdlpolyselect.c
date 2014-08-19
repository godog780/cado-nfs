/**
 * \file cado-nfs/polyselect/gfpkdlpolyselect.c
 * 
 * \date 08/08/2014
 * \author Aurore Guillevic
 * \email guillevic@lix.polytechnique.fr
 * \brief Compute two polynomials f, g suitable for discrete logarithm in 
 *        extension fields, with the conjugation method.
 *
 * \test TODO
 */


#include "cado.h"
#include "auxiliary.h"
#include "area.h"
#include "utils.h"
#include "portability.h"
#include "murphyE.h"
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
//#include <time.h> 

#include "cado_poly.h"

#define DEG_PY 2
#if DEG_PY > 2
#error "the code works only for Py of degree <= 2, sorry."
#endif
#define VARPHI_COEFF_INT 1

//typedef int coeff_t;

typedef struct {
  int t;     // parameter t
  int PY[DEG_PY + 1]; // no --> use one of the poly stuct of cado-nfs!
  int f[MAXDEGREE + 1];  // polynomial f of degree at most MAXDEGREE 
                         // set to 10 at the moment in cado_poly.h
}row_f_poly_t;

// tables containing polynomials f
// how to encode varphi form ?
typedef struct {
  unsigned int deg_f;
  unsigned int deg_Py;
  unsigned int deg_varphi;
  row_f_poly_t* table_f;
  int varphi[MAXDEGREE + 1][DEG_PY]; // poly whose coefficients are themselves poly in Y 
  //(a root of PY) modulo PY so of degree at most DEG_PY-1 --> of DEG_PY coeffs.
}table_f_poly_t;

#include "table_t_Py_f_deg4_type0_h1_t-200--200.c"
table_f_poly_t table_f4;
table_f4.table_f = TAB_f4_CYCLIC; // both pointers
table_f4.deg_f = 4;
table_f4.deg_varphi = 2;
table_f4.deg_Py = 2;
table_f4.varphi = {{-1, 0}, {0, 1}, {1,0}}; // -1 + y*X + X^2

/**
 * \brief evaluate varphi at (u,v) and outputs a polynomial g, 
 *        assuming that Py is of degree 2.
 * 
 * \param[in] table_f structure that contains varphi and its degree
 * \param[in] u  mpz_t integer, of size half the size of p
 * \param[in] v  mpz_t integer, of size half the size of p
 *               u/v = y mod p with y a root of PY mod p.
 * \param[out] g polynomial g (already initialized)
 * 
 * NOTE: maybe input varphi and deg_varphi directly ? Or a poly structure ?
 */
// works only if PY is of degree 2
void eval_varphi_mpz(mpz_poly_t g, mpz_t** varphi_coeff, int deg_varphi, mpz_t u, mpz_t v)
{
  int i;
  for (i=0; i <= deg_varphi; i++){
    mpz_mul(g->coeff[i], v, varphi_coeff[i][0]);    // gi <- varphi_i0 * v
    mpz_addmul(g->coeff[i], u, varphi_coeff[i][1]); // gi <- gi + varphi_i1 * u
  }
}
    // t[0] + t[1]*X + ... + t[deg_varphi]*X^deg_varphi
    // with t[i] = t[i][0] + t[i][1]*Y + ... t[i][deg_Py - 1]*Y^(deg_Py-1)
    // t[i][j] are int
    // u, v are mpz_t (multi precision integers)
    // table_f.varphi[i][0] * v +
    // table_f.varphi[i][1] * u
    // varphi_i = varphi_i0 + varphi_i1 * Y
    // the function does not use modular arithmetic but exact integer arithmetic
void eval_varphi_si(mpz_poly_t g, long int** varphi_coeff, int deg_varphi, mpz_t u, mpz_t v)
{
  int i;
  for (i=0; i <= deg_varphi; i++){
    // if varphi has coefficients of type (signed) long int
    mpz_mul_si(g->coeff[i], v, varphi_coeff[i][0]);    // gi <- varphi_i0 * v
    mpz_addmul_si(g->coeff[i], u, varphi_coeff[i][1]); // gi <- gi + varphi_i1 * u
  }
}


// for MurphyE value
double area=AREA;
double bound_f=BOUND_F;
double bound_g=BOUND_G;

/**
 * \brief return a pointer to a table [{t, PY, f}] with f of degree k.
 * 
 * \param[in] 
 * \param[in] deg_f integer greater than 1 ( deg_f = 4 or 6 implemented for now)
 * \param[out] table_f (pointer to a) table of polynomials f (the one of higher degree)
 *                     of degree deg_f, NULL if there is no such table for deg_f.
 * \param[out] table_f_size* size of the returned table 
 * 
 */
bool polygen_CONJ_get_tab_f(unsigned int deg_f, \
			    table_f_poly_t* table_f, \
			    unsigned int* table_f_size)
{
  switch (deg_f){
  case 4:
    table_f = TAB_f4_CYCLIC;
    *table_f_size = TAB_f4_CYCLIC_SIZE;
    return true;
  case 6:
    table_f = TAB_f6_CYCLIC;
    *table_f_size = TAB_f6_CYCLIC_SIZE;
    return true;
  default:
    table_f = NULL;
    *table_f_size = O;
    return false;
  }
}



/**
 * \brief test wether varphi is a suitable candidate for computing g from it
 * 
 * \param[in] p multiprecision integer, prime
 * \param[in] k integer greater than 1 ( k = 2 or 3 at the moment)
 * \param[in] varphi polynomial with coefficients mod p (so very large)
 *
 * \return bool true if varphi is of degree k and irreducible mod p
 * \return false otherwise
 */
bool is_good_varphi(mpz_poly_t varphi, unsigned int k, mpz_t p)
{
  //    return (Degree(varphi_p) eq k) and IsIrreducible(varphi_p);


}

/**
 * \brief 
 * 
 * \param[in] 
 * \param[in] 
 * \param[out] 
 * \param[out] 
 * 
 */
bool is_good_f_PY()
{


}

/**
 * \brief select suitable polynomial f 
 * 
 * \param[in] p multiprecision integer, prime
 * \param[in] k integer greater than 1 ( k = 2 or 3 at the moment)
 * \param[out] f polynomial (the one of higher degree)
 * 
 */
// note: mpz_poly_t is a 1-dim 1-element array. So this is a pointer to 
// an initialized thing.
void polygen_CONJ_f ( mpz_t p, unsigned int k, mpz_poly_t f )
{
  /*which f table to choose ? */
  table_f_poly_t* table_f;
  unsigned int table_f_size;
  polygen_CONJ_get_tab_f(k, &table_f, &table_f_size);
  
  while ()// nothing found
    // i.e. the i-th polynomial in f is not irreducible mod p,
    // or is but PY has no root.
    {

    }
}

/**
 * \brief select suitable polynomial g according to f, p, k 
 * 
 * \param[in] p multiprecision integer, prime
 * \param[in] k integer greater than 1 ( k = 2 or 3 at the moment)
 * \param[in] f polynomial (the one of higher degree)
 * \param[out] g polynomial (the one of lower degree)
 *
 */
void polygen_CONJ_g ( mpz_t p, unsigned int k, mpz_poly_t f, mpz_poly_t g )
{

}

