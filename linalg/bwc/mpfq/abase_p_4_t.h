#ifndef ABASE_P_4_T_H_
#define ABASE_P_4_T_H_

/* MPFQ generated file -- do not edit */

#include "abase_p_4.h"
/* Active handler: simd_gfp */
/* Automatically generated code  */
/* Active handler: Mpfq::defaults */
/* Active handler: Mpfq::defaults::vec */
/* Active handler: Mpfq::defaults::poly */
/* Active handler: Mpfq::gfp::field */
/* Active handler: Mpfq::gfp::elt */
/* Active handler: Mpfq::defaults::mpi_flat */
/* Options used: w=64 fieldtype=prime n=4 nn=9 vtag=p_4 vbase_stuff={
                 'vc:includes' => [
                                    '<stdarg.h>'
                                  ],
                 'member_templates_restrict' => {
                                                  'p_4' => [
                                                             'p_4'
                                                           ],
                                                  'u64k2' => [
                                                               'u64k1',
                                                               'u64k2'
                                                             ],
                                                  'p16' => [
                                                             'p16'
                                                           ],
                                                  'u64k1' => $vbase_stuff->{'member_templates_restrict'}{'u64k2'}
                                                },
                 'families' => [
                                 $vbase_stuff->{'member_templates_restrict'}{'u64k2'},
                                 $vbase_stuff->{'member_templates_restrict'}{'p16'},
                                 $vbase_stuff->{'member_templates_restrict'}{'p_4'}
                               ],
                 'choose_byfeatures' => sub { "DUMMY" }
               };
 tag=p_4 type=plain virtual_base={
                  'filebase' => 'abase_vbase',
                  'substitutions' => [
                                       [
                                         qr/(?^:abase_p_4_elt \*)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_src_elt\b)/,
                                         'const void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_elt\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_dst_elt\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_elt_ur \*)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_src_elt_ur\b)/,
                                         'const void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_elt_ur\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_dst_elt_ur\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_vec \*)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_src_vec\b)/,
                                         'const void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_vec\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_dst_vec\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_vec_ur \*)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_src_vec_ur\b)/,
                                         'const void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_vec_ur\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_dst_vec_ur\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_poly \*)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_src_poly\b)/,
                                         'const void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_poly\b)/,
                                         'void *'
                                       ],
                                       [
                                         qr/(?^:abase_p_4_dst_poly\b)/,
                                         'void *'
                                       ]
                                     ],
                  'name' => 'abase_vbase',
                  'global_prefix' => 'abase_'
                };
 family=[p_4] */


/* Functions operating on the field structure */

/* Element allocation functions */

/* Elementary assignment functions */

/* Assignment of random values */

/* Arithmetic operations on elements */

/* Operations involving unreduced elements */

/* Comparison functions */

/* Input/output functions */

/* Vector functions */

/* Functions related to SIMD operation */

/* Member templates related to SIMD operation */

/* MPI interface */

/* Object-oriented interface */
void abase_p_4_p_4_dotprod(abase_p_4_dst_field, abase_p_4_dst_field, abase_p_4_dst_vec, abase_p_4_src_vec, abase_p_4_src_vec, unsigned int);
void abase_p_4_p_4_addmul_tiny(abase_p_4_dst_field, abase_p_4_dst_field, abase_p_4_dst_vec, abase_p_4_src_vec, abase_p_4_dst_vec, unsigned int);
void abase_p_4_p_4_transpose(abase_p_4_dst_field, abase_p_4_dst_field, abase_p_4_dst_vec, abase_p_4_src_vec);

#endif  /* ABASE_P_4_T_H_ */

/* vim:set ft=cpp: */
