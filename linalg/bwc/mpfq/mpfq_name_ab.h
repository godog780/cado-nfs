#ifndef MPFQ_NAME_AB_H_
#define MPFQ_NAME_AB_H_

/* Automatically generated file.
 *
 * This header file is just wrap-up code for accessing a global finite
 * field with convenient aliases.
 *
 * Note that this file is automatically generated from the mpfq api, and
 * is therefore guaranteed to contain all the api functions usable in the
 * current realm.
 */

#ifndef MPFQ_LAST_GENERATED_TAG
#error "Please include an mpfq-generated header first"
#endif

/* cpp has its infelicities. Yes the extra step is needed */
#ifndef MPFQ_CONCAT4
#define MPFQ_CONCAT4(X,Y,Z,T) X ## Y ## Z ## T
#endif

#ifndef MPFQ_CREATE_FUNCTION_NAME
#define MPFQ_CREATE_FUNCTION_NAME(TAG,NAME) MPFQ_CONCAT4(abase_,TAG,_,NAME)
#endif

#define abcommon_name_(X) MPFQ_CREATE_FUNCTION_NAME(MPFQ_LAST_GENERATED_TAG,X)

#define abfield	abcommon_name_(field)
#define abdst_field	abcommon_name_(dst_field)

#define abelt	abcommon_name_(elt)
#define abdst_elt	abcommon_name_(dst_elt)
#define absrc_elt	abcommon_name_(src_elt)

#define abelt_ur	abcommon_name_(elt_ur)
#define abdst_elt_ur	abcommon_name_(dst_elt_ur)
#define absrc_elt_ur	abcommon_name_(src_elt_ur)

#define abvec	abcommon_name_(vec)
#define abdst_vec	abcommon_name_(dst_vec)
#define absrc_vec	abcommon_name_(src_vec)

#define abvec_ur	abcommon_name_(vec_ur)
#define abdst_vec_ur	abcommon_name_(dst_vec_ur)
#define absrc_vec_ur	abcommon_name_(src_vec_ur)

#define abpoly	abcommon_name_(poly)
#define abdst_poly	abcommon_name_(dst_poly)
#define absrc_poly	abcommon_name_(src_poly)





#define abfield_characteristic(a0,a1)	abcommon_name_(field_characteristic) (a0,a1)
#define abfield_degree(a0)	abcommon_name_(field_degree) (a0)
#define abfield_init(a0)	abcommon_name_(field_init) (a0)
#define abfield_clear(a0)	abcommon_name_(field_clear) (a0)
#define abfield_specify(a0,a1,a2)	abcommon_name_(field_specify) (a0,a1,a2)
#define abfield_setopt(a0,a1,a2)	abcommon_name_(field_setopt) (a0,a1,a2)

#define abinit(a0,a1)	abcommon_name_(init) (a0,a1)
#define abclear(a0,a1)	abcommon_name_(clear) (a0,a1)

#define abset(a0,a1,a2)	abcommon_name_(set) (a0,a1,a2)
#define abset_ui(a0,a1,a2)	abcommon_name_(set_ui) (a0,a1,a2)
#define abset_zero(a0,a1)	abcommon_name_(set_zero) (a0,a1)
#define abget_ui(a0,a1)	abcommon_name_(get_ui) (a0,a1)
#define abset_mpn(a0,a1,a2,a3)	abcommon_name_(set_mpn) (a0,a1,a2,a3)
#define abset_mpz(a0,a1,a2)	abcommon_name_(set_mpz) (a0,a1,a2)
#define abget_mpn(a0,a1,a2)	abcommon_name_(get_mpn) (a0,a1,a2)
#define abget_mpz(a0,a1,a2)	abcommon_name_(get_mpz) (a0,a1,a2)

#define abset_uipoly(a0,a1,a2)	abcommon_name_(set_uipoly) (a0,a1,a2)
#define abset_uipoly_wide(a0,a1,a2,a3)	abcommon_name_(set_uipoly_wide) (a0,a1,a2,a3)
#define abget_uipoly(a0,a1)	abcommon_name_(get_uipoly) (a0,a1)
#define abget_uipoly_wide(a0,a1,a2)	abcommon_name_(get_uipoly_wide) (a0,a1,a2)


#define abrandom(a0,a1)	abcommon_name_(random) (a0,a1)
#define abrandom2(a0,a1)	abcommon_name_(random2) (a0,a1)

#define abadd(a0,a1,a2,a3)	abcommon_name_(add) (a0,a1,a2,a3)
#define absub(a0,a1,a2,a3)	abcommon_name_(sub) (a0,a1,a2,a3)
#define abneg(a0,a1,a2)	abcommon_name_(neg) (a0,a1,a2)
#define abmul(a0,a1,a2,a3)	abcommon_name_(mul) (a0,a1,a2,a3)
#define absqr(a0,a1,a2)	abcommon_name_(sqr) (a0,a1,a2)
#define abis_sqr(a0,a1)	abcommon_name_(is_sqr) (a0,a1)
#define absqrt(a0,a1,a2)	abcommon_name_(sqrt) (a0,a1,a2)
#define abpow(a0,a1,a2,a3,a4)	abcommon_name_(pow) (a0,a1,a2,a3,a4)
#define abfrobenius(a0,a1,a2)	abcommon_name_(frobenius) (a0,a1,a2)
#define abadd_ui(a0,a1,a2,a3)	abcommon_name_(add_ui) (a0,a1,a2,a3)
#define absub_ui(a0,a1,a2,a3)	abcommon_name_(sub_ui) (a0,a1,a2,a3)
#define abmul_ui(a0,a1,a2,a3)	abcommon_name_(mul_ui) (a0,a1,a2,a3)
#define abadd_uipoly(a0,a1,a2,a3)	abcommon_name_(add_uipoly) (a0,a1,a2,a3)
#define absub_uipoly(a0,a1,a2,a3)	abcommon_name_(sub_uipoly) (a0,a1,a2,a3)
#define abmul_uipoly(a0,a1,a2,a3)	abcommon_name_(mul_uipoly) (a0,a1,a2,a3)
#define abinv(a0,a1,a2)	abcommon_name_(inv) (a0,a1,a2)
#define abas_solve(a0,a1,a2)	abcommon_name_(as_solve) (a0,a1,a2)
#define abtrace(a0,a1)	abcommon_name_(trace) (a0,a1)
#define abhadamard(a0,a1,a2,a3,a4)	abcommon_name_(hadamard) (a0,a1,a2,a3,a4)

#define abelt_ur_init(a0,a1)	abcommon_name_(elt_ur_init) (a0,a1)
#define abelt_ur_clear(a0,a1)	abcommon_name_(elt_ur_clear) (a0,a1)
#define abelt_ur_set(a0,a1,a2)	abcommon_name_(elt_ur_set) (a0,a1,a2)
#define abelt_ur_set_zero(a0,a1)	abcommon_name_(elt_ur_set_zero) (a0,a1)
#define abelt_ur_set_ui(a0,a1,a2)	abcommon_name_(elt_ur_set_ui) (a0,a1,a2)
#define abelt_ur_add(a0,a1,a2,a3)	abcommon_name_(elt_ur_add) (a0,a1,a2,a3)
#define abelt_ur_neg(a0,a1,a2)	abcommon_name_(elt_ur_neg) (a0,a1,a2)
#define abelt_ur_sub(a0,a1,a2,a3)	abcommon_name_(elt_ur_sub) (a0,a1,a2,a3)
#define abmul_ur(a0,a1,a2,a3)	abcommon_name_(mul_ur) (a0,a1,a2,a3)
#define absqr_ur(a0,a1,a2)	abcommon_name_(sqr_ur) (a0,a1,a2)
#define abreduce(a0,a1,a2)	abcommon_name_(reduce) (a0,a1,a2)
#define abaddmul_si_ur(a0,a1,a2,a3)	abcommon_name_(addmul_si_ur) (a0,a1,a2,a3)

#define abcmp(a0,a1,a2)	abcommon_name_(cmp) (a0,a1,a2)
#define abcmp_ui(a0,a1,a2)	abcommon_name_(cmp_ui) (a0,a1,a2)
#define abis_zero(a0,a1)	abcommon_name_(is_zero) (a0,a1)


#define abmgy_enc(a0,a1,a2)	abcommon_name_(mgy_enc) (a0,a1,a2)
#define abmgy_dec(a0,a1,a2)	abcommon_name_(mgy_dec) (a0,a1,a2)


#define abasprint(a0,a1,a2)	abcommon_name_(asprint) (a0,a1,a2)
#define abfprint(a0,a1,a2)	abcommon_name_(fprint) (a0,a1,a2)
#define abprint(a0,a1)	abcommon_name_(print) (a0,a1)
#define absscan(a0,a1,a2)	abcommon_name_(sscan) (a0,a1,a2)
#define abfscan(a0,a1,a2)	abcommon_name_(fscan) (a0,a1,a2)
#define abscan(a0,a1)	abcommon_name_(scan) (a0,a1)



#define abvec_init(a0,a1,a2)	abcommon_name_(vec_init) (a0,a1,a2)
#define abvec_reinit(a0,a1,a2,a3)	abcommon_name_(vec_reinit) (a0,a1,a2,a3)
#define abvec_clear(a0,a1,a2)	abcommon_name_(vec_clear) (a0,a1,a2)
#define abvec_set(a0,a1,a2,a3)	abcommon_name_(vec_set) (a0,a1,a2,a3)
#define abvec_set_zero(a0,a1,a2)	abcommon_name_(vec_set_zero) (a0,a1,a2)
#define abvec_setcoef(a0,a1,a2,a3)	abcommon_name_(vec_setcoef) (a0,a1,a2,a3)
#define abvec_setcoef_ui(a0,a1,a2,a3)	abcommon_name_(vec_setcoef_ui) (a0,a1,a2,a3)
#define abvec_getcoef(a0,a1,a2,a3)	abcommon_name_(vec_getcoef) (a0,a1,a2,a3)
#define abvec_add(a0,a1,a2,a3,a4)	abcommon_name_(vec_add) (a0,a1,a2,a3,a4)
#define abvec_neg(a0,a1,a2,a3)	abcommon_name_(vec_neg) (a0,a1,a2,a3)
#define abvec_rev(a0,a1,a2,a3)	abcommon_name_(vec_rev) (a0,a1,a2,a3)
#define abvec_sub(a0,a1,a2,a3,a4)	abcommon_name_(vec_sub) (a0,a1,a2,a3,a4)
#define abvec_scal_mul(a0,a1,a2,a3,a4)	abcommon_name_(vec_scal_mul) (a0,a1,a2,a3,a4)
#define abvec_conv(a0,a1,a2,a3,a4,a5)	abcommon_name_(vec_conv) (a0,a1,a2,a3,a4,a5)
#define abvec_random(a0,a1,a2)	abcommon_name_(vec_random) (a0,a1,a2)
#define abvec_random2(a0,a1,a2)	abcommon_name_(vec_random2) (a0,a1,a2)
#define abvec_cmp(a0,a1,a2,a3)	abcommon_name_(vec_cmp) (a0,a1,a2,a3)
#define abvec_is_zero(a0,a1,a2)	abcommon_name_(vec_is_zero) (a0,a1,a2)
#define abvec_asprint(a0,a1,a2,a3)	abcommon_name_(vec_asprint) (a0,a1,a2,a3)
#define abvec_fprint(a0,a1,a2,a3)	abcommon_name_(vec_fprint) (a0,a1,a2,a3)
#define abvec_print(a0,a1,a2)	abcommon_name_(vec_print) (a0,a1,a2)
#define abvec_sscan(a0,a1,a2,a3)	abcommon_name_(vec_sscan) (a0,a1,a2,a3)
#define abvec_fscan(a0,a1,a2,a3)	abcommon_name_(vec_fscan) (a0,a1,a2,a3)
#define abvec_scan(a0,a1,a2)	abcommon_name_(vec_scan) (a0,a1,a2)
#define abvec_ur_init(a0,a1,a2)	abcommon_name_(vec_ur_init) (a0,a1,a2)
#define abvec_ur_reinit(a0,a1,a2,a3)	abcommon_name_(vec_ur_reinit) (a0,a1,a2,a3)
#define abvec_ur_clear(a0,a1,a2)	abcommon_name_(vec_ur_clear) (a0,a1,a2)
#define abvec_ur_set(a0,a1,a2,a3)	abcommon_name_(vec_ur_set) (a0,a1,a2,a3)
#define abvec_ur_setcoef(a0,a1,a2,a3)	abcommon_name_(vec_ur_setcoef) (a0,a1,a2,a3)
#define abvec_ur_getcoef(a0,a1,a2,a3)	abcommon_name_(vec_ur_getcoef) (a0,a1,a2,a3)
#define abvec_ur_add(a0,a1,a2,a3,a4)	abcommon_name_(vec_ur_add) (a0,a1,a2,a3,a4)
#define abvec_ur_sub(a0,a1,a2,a3,a4)	abcommon_name_(vec_ur_sub) (a0,a1,a2,a3,a4)
#define abvec_scal_mul_ur(a0,a1,a2,a3,a4)	abcommon_name_(vec_scal_mul_ur) (a0,a1,a2,a3,a4)
#define abvec_conv_ur(a0,a1,a2,a3,a4,a5)	abcommon_name_(vec_conv_ur) (a0,a1,a2,a3,a4,a5)
#define abvec_reduce(a0,a1,a2,a3)	abcommon_name_(vec_reduce) (a0,a1,a2,a3)
#define abvec_elt_stride(a0,a1)	abcommon_name_(vec_elt_stride) (a0,a1)



#define abpoly_init(a0,a1,a2)	abcommon_name_(poly_init) (a0,a1,a2)
#define abpoly_clear(a0,a1)	abcommon_name_(poly_clear) (a0,a1)
#define abpoly_set(a0,a1,a2)	abcommon_name_(poly_set) (a0,a1,a2)
#define abpoly_setmonic(a0,a1,a2)	abcommon_name_(poly_setmonic) (a0,a1,a2)
#define abpoly_setcoef(a0,a1,a2,a3)	abcommon_name_(poly_setcoef) (a0,a1,a2,a3)
#define abpoly_setcoef_ui(a0,a1,a2,a3)	abcommon_name_(poly_setcoef_ui) (a0,a1,a2,a3)
#define abpoly_getcoef(a0,a1,a2,a3)	abcommon_name_(poly_getcoef) (a0,a1,a2,a3)
#define abpoly_deg(a0,a1)	abcommon_name_(poly_deg) (a0,a1)
#define abpoly_add(a0,a1,a2,a3)	abcommon_name_(poly_add) (a0,a1,a2,a3)
#define abpoly_sub(a0,a1,a2,a3)	abcommon_name_(poly_sub) (a0,a1,a2,a3)
#define abpoly_add_ui(a0,a1,a2,a3)	abcommon_name_(poly_add_ui) (a0,a1,a2,a3)
#define abpoly_sub_ui(a0,a1,a2,a3)	abcommon_name_(poly_sub_ui) (a0,a1,a2,a3)
#define abpoly_neg(a0,a1,a2)	abcommon_name_(poly_neg) (a0,a1,a2)
#define abpoly_scal_mul(a0,a1,a2,a3)	abcommon_name_(poly_scal_mul) (a0,a1,a2,a3)
#define abpoly_mul(a0,a1,a2,a3)	abcommon_name_(poly_mul) (a0,a1,a2,a3)
#define abpoly_divmod(a0,a1,a2,a3,a4)	abcommon_name_(poly_divmod) (a0,a1,a2,a3,a4)
#define abpoly_precomp_mod(a0,a1,a2)	abcommon_name_(poly_precomp_mod) (a0,a1,a2)
#define abpoly_mod_pre(a0,a1,a2,a3,a4)	abcommon_name_(poly_mod_pre) (a0,a1,a2,a3,a4)
#define abpoly_gcd(a0,a1,a2,a3)	abcommon_name_(poly_gcd) (a0,a1,a2,a3)
#define abpoly_xgcd(a0,a1,a2,a3,a4,a5)	abcommon_name_(poly_xgcd) (a0,a1,a2,a3,a4,a5)
#define abpoly_random(a0,a1,a2)	abcommon_name_(poly_random) (a0,a1,a2)
#define abpoly_random2(a0,a1,a2)	abcommon_name_(poly_random2) (a0,a1,a2)
#define abpoly_cmp(a0,a1,a2)	abcommon_name_(poly_cmp) (a0,a1,a2)
#define abpoly_asprint(a0,a1,a2)	abcommon_name_(poly_asprint) (a0,a1,a2)
#define abpoly_fprint(a0,a1,a2)	abcommon_name_(poly_fprint) (a0,a1,a2)
#define abpoly_print(a0,a1)	abcommon_name_(poly_print) (a0,a1)
#define abpoly_sscan(a0,a1,a2)	abcommon_name_(poly_sscan) (a0,a1,a2)
#define abpoly_fscan(a0,a1,a2)	abcommon_name_(poly_fscan) (a0,a1,a2)
#define abpoly_scan(a0,a1)	abcommon_name_(poly_scan) (a0,a1)



#define abgroupsize(a0)	abcommon_name_(groupsize) (a0)
#define aboffset(a0,a1)	abcommon_name_(offset) (a0,a1)
#define abstride(a0)	abcommon_name_(stride) (a0)
#define abset_ui_at(a0,a1,a2,a3)	abcommon_name_(set_ui_at) (a0,a1,a2,a3)
#define abset_ui_all(a0,a1,a2)	abcommon_name_(set_ui_all) (a0,a1,a2)
#define abelt_ur_set_ui_at(a0,a1,a2,a3)	abcommon_name_(elt_ur_set_ui_at) (a0,a1,a2,a3)
#define abelt_ur_set_ui_all(a0,a1,a2)	abcommon_name_(elt_ur_set_ui_all) (a0,a1,a2)
#define abdotprod(a0,a1,a2,a3,a4)	abcommon_name_(dotprod) (a0,a1,a2,a3,a4)
#define abmul_constant_ui(a0,a1,a2,a3)	abcommon_name_(mul_constant_ui) (a0,a1,a2,a3)


#define abmember_template_dotprod(a0,a1,a2,a3,a4,a5)	abcommon_name_(member_template_dotprod) (a0,a1,a2,a3,a4,a5)
#define abmember_template_addmul_tiny(a0,a1,a2,a3,a4,a5)	abcommon_name_(member_template_addmul_tiny) (a0,a1,a2,a3,a4,a5)
#define abmember_template_transpose(a0,a1,a2,a3)	abcommon_name_(member_template_transpose) (a0,a1,a2,a3)




#define abmpi_ops_init(a0)	abcommon_name_(mpi_ops_init) (a0)
#define abmpi_datatype(a0)	abcommon_name_(mpi_datatype) (a0)
#define abmpi_datatype_ur(a0)	abcommon_name_(mpi_datatype_ur) (a0)
#define abmpi_addition_op(a0)	abcommon_name_(mpi_addition_op) (a0)
#define abmpi_addition_op_ur(a0)	abcommon_name_(mpi_addition_op_ur) (a0)
#define abmpi_ops_clear(a0)	abcommon_name_(mpi_ops_clear) (a0)




#define aboo_impl_name(a0)	abcommon_name_(oo_impl_name) (a0)
#define aboo_field_init(a0)	abcommon_name_(oo_field_init) (a0)
#define aboo_field_clear(a0)	abcommon_name_(oo_field_clear) (a0)


/* another customary shorthand */
#define	abdegree	abfield_degree()


#endif  /* MPFQ_NAME_AB_H_ */
