/*
 * MacRuby kernel. This file is compiled into LLVM bitcode and injected into
 * the global module. Some of the functions defined here are inlined in the
 * code MacRuby generates.
 *
 * This file is covered by the Ruby license. See COPYING for more details.
 * 
 * Copyright (C) 2010, Apple Inc. All rights reserved.
 */

#include "ruby/ruby.h"
#include "ruby/node.h"
#include "vm.h"
#include "compiler.h"
#include "bridgesupport.h"
#include "id.h"
#include "array.h"
#include "hash.h"
#include "encoding.h"
#include "class.h"
#include "objc.h"

inline VALUE
vm_ivar_get(VALUE obj, ID name, void *cache_p)
{
    struct icache *cache = (struct icache *)cache_p;
    VALUE klass = 0;

    if (!SPECIAL_CONST_P(obj)) {
	klass = *(VALUE *)obj;
	if (klass == cache->klass) {
	    if ((unsigned int)cache->slot < ROBJECT(obj)->num_slots) {
		rb_object_ivar_slot_t *slot;
use_slot:
		slot = &ROBJECT(obj)->slots[cache->slot];
		if (slot->name == name) {
		    VALUE val = slot->value;
		    if (val == Qundef) {
			val = Qnil;
		    }
		    return val;
		}
	    }
	}
    }

    if (cache->slot == SLOT_CACHE_VIRGIN) {
	const int slot = rb_vm_get_ivar_slot(obj, name, true);
	if (slot >= 0) {
	    cache->klass = *(VALUE *)obj;
	    cache->slot = slot;
	    goto use_slot;
	}
	cache->klass = 0;
	cache->slot = SLOT_CACHE_CANNOT;
    }

    return rb_ivar_get(obj, name);
}

inline void
vm_ivar_set(VALUE obj, ID name, VALUE val, void *cache_p)
{
    struct icache *cache = (struct icache *)cache_p; 
    VALUE klass = 0;

    if (!SPECIAL_CONST_P(obj)) {
	klass = *(VALUE *)obj;
	if (klass == cache->klass) {
	    if ((unsigned int)cache->slot < ROBJECT(obj)->num_slots) {
		rb_object_ivar_slot_t *slot;
use_slot:
		slot = &ROBJECT(obj)->slots[cache->slot];
		if (slot->name == name) {
		    if ((ROBJECT(obj)->basic.flags & FL_FREEZE) == FL_FREEZE) {
			rb_error_frozen("object");
		    }
		    GC_WB(&slot->value, val);
		    return;
		}
	    }
	}
    }

    if (cache->slot == SLOT_CACHE_VIRGIN) {
	const int slot = rb_vm_get_ivar_slot(obj, name, true);
	if (slot >= 0) {
	    cache->klass = *(VALUE *)obj;
	    cache->slot = slot;
	    goto use_slot;
	}
	cache->slot = SLOT_CACHE_CANNOT;
    }

    rb_ivar_set(obj, name, val);
}

inline VALUE
vm_cvar_get(VALUE klass, ID id, unsigned char check,
	unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    return rb_cvar_get2(klass, id, check);
}

inline VALUE
vm_cvar_set(VALUE klass, ID id, VALUE val, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (k != NULL) {
	    klass = (VALUE)k;
	}
    }
    rb_cvar_set(klass, id, val);
    return val;
}

inline VALUE
vm_get_const(VALUE outer, void *cache_p, ID path, int flags)
{
    struct ccache *cache = (struct ccache *) cache_p;
    const bool lexical_lookup = (flags & CONST_LOOKUP_LEXICAL);
    const bool dynamic_class = (flags & CONST_LOOKUP_DYNAMIC_CLASS);

    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (lexical_lookup && k != NULL) {
	    outer = (VALUE)k;
	}
    }

    VALUE val;
    if (cache->outer == outer && cache->val != Qundef) {
	val = cache->val;
    }
    else {
	val = rb_vm_const_lookup(outer, path, lexical_lookup, false);
	cache->outer = outer;
	cache->val = val;
    }

    return val;
}

inline void 
vm_set_const(VALUE outer, ID id, VALUE obj, unsigned char dynamic_class)
{
    if (dynamic_class) {
	Class k = rb_vm_get_current_class();
	if (k != NULL) {
	    outer = (VALUE)k;
	}
    }
    rb_const_set(outer, id, obj);
}

VALUE rb_vm_dispatch(void *cache, VALUE top, VALUE self, void *sel,
	void *block, unsigned char opt, int argc, ...);

// Only numeric immediates have their lsb at 1.
#define NUMERIC_IMM_P(x) ((x & 0x1) == 0x1)

#define IMM2DBL(x) (FIXFLOAT_P(x) ? FIXFLOAT2DBL(x) : FIX2LONG(x))

inline VALUE
vm_fast_plus(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long res = FIX2LONG(left) + FIX2LONG(right);
	    if (FIXABLE(res)) {
		return LONG2FIX(res);
	    }
	}
	else {
	    const double res = IMM2DBL(left) + IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return rb_vm_dispatch(cache, 0, left, selPLUS, NULL, 0, 1, right);
}

inline VALUE
vm_fast_minus(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long res = FIX2LONG(left) - FIX2LONG(right);
	    if (FIXABLE(res)) {
		return LONG2FIX(res);
	    }
	}
	else {
	    const double res = IMM2DBL(left) - IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return rb_vm_dispatch(cache, 0, left, selMINUS, NULL, 0, 1, right);
}

inline VALUE
vm_fast_mult(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long res = FIX2LONG(left) * FIX2LONG(right);
	    if (FIXABLE(res)) {
		return LONG2FIX(res);
	    }
	}
	else {
	    const double res = IMM2DBL(left) * IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return rb_vm_dispatch(cache, 0, left, selMULT, NULL, 0, 1, right);
}

inline VALUE
vm_fast_div(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    const long x = FIX2LONG(left);
	    const long y = FIX2LONG(right);
	    if (y != 0) {
		long res = x / y;
		if (((x < 0 && y >= 0) || (x >= 0 && y < 0))
			&& (x % y) != 0) {
		    res--;
		}
		if (FIXABLE(res)) {
		    return LONG2FIX(res);
		}
	    }
	}
	else {
	    const double res = IMM2DBL(left) / IMM2DBL(right);
	    return DBL2FIXFLOAT(res);
	}
    }
    return rb_vm_dispatch(cache, 0, left, selDIV, NULL, 0, 1, right);
}

inline VALUE
vm_fast_lt(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) < FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) < IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return rb_vm_dispatch(cache, 0, left, selLT, NULL, 0, 1, right);
}

inline VALUE
vm_fast_le(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) <= FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) <= IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return rb_vm_dispatch(cache, 0, left, selLE, NULL, 0, 1, right);
}

inline VALUE
vm_fast_gt(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) > FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) > IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return rb_vm_dispatch(cache, 0, left, selGT, NULL, 0, 1, right);
}

inline VALUE
vm_fast_ge(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0 && NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	if (FIXNUM_P(left) && FIXNUM_P(right)) {
	    return FIX2LONG(left) >= FIX2LONG(right) ? Qtrue : Qfalse;
	}
	else {
	    return IMM2DBL(left) >= IMM2DBL(right) ? Qtrue : Qfalse;
	}
    }
    return rb_vm_dispatch(cache, 0, left, selGE, NULL, 0, 1, right);
}

inline VALUE
vm_fast_eq(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) == FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) == IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	}
	if (left == Qtrue || left == Qfalse) {
	    return left == right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return rb_vm_dispatch(cache, 0, left, selEq, NULL, 0, 1, right);
}

inline VALUE
vm_fast_eqq(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) == FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) == IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	}
	if (left == Qtrue || left == Qfalse) {
	    return left == right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return rb_vm_dispatch(cache, 0, left, selEqq, NULL, 0, 1, right);
}

inline VALUE
vm_fast_neq(void *cache, VALUE left, VALUE right, unsigned char overriden)
{
    if (overriden == 0) {
	if (NUMERIC_IMM_P(left) && NUMERIC_IMM_P(right)) {
	    if (FIXNUM_P(left) && FIXNUM_P(right)) {
		return FIX2LONG(left) != FIX2LONG(right) ? Qtrue : Qfalse;
	    }
	    else {
		return IMM2DBL(left) != IMM2DBL(right) ? Qtrue : Qfalse;
	    }
	} 
	if (left == Qtrue || left == Qfalse) {
	    return left != right ? Qtrue : Qfalse;
	}
	// TODO: opt for non-immediate types
    }
    return rb_vm_dispatch(cache, 0, left, selNeq, NULL, 0, 1, right);
}

inline VALUE
vm_fast_aref(VALUE obj, VALUE other, void *cache, unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    if (FIXNUM_P(other)) {
		return rary_entry(obj, FIX2LONG(other));
	    }
	}
	else if (klass == rb_cRubyHash) {
	    VALUE val = rhash_lookup(obj, other);
	    if (val == Qundef) {
		if (RHASH(obj)->ifnone == Qnil) {
		    return Qnil;
		}
		return rhash_call_default(obj, other);
	    }
	    return val;
	}
    }
    return rb_vm_dispatch(cache, 0, obj, selAREF, NULL, 0, 1, other);
}

inline VALUE
vm_fast_aset(VALUE obj, VALUE other1, VALUE other2, void *cache,
	unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    if (FIXNUM_P(other1)) {
		rary_store(obj, FIX2LONG(other1), other2);
		return other2;
	    }
	}
	else if (klass == rb_cRubyHash) {
	    return rhash_aset(obj, 0, other1, other2);
	}
    }
    return rb_vm_dispatch(cache, 0, obj, selASET, NULL, 0, 2, other1, other2);
}

inline VALUE
vm_fast_shift(VALUE obj, VALUE other, void *cache, unsigned char overriden)
{
    if (overriden == 0 && !SPECIAL_CONST_P(obj)) {
	VALUE klass = *(VALUE *)obj;
	if (klass == rb_cRubyArray) {
	    rary_modify(obj);
	    rary_push(obj, other);
	    return obj;
	}
	else if (klass == rb_cRubyString) {
	    return rstr_concat(obj, 0, other);
	}
    }
    return rb_vm_dispatch(cache, 0, obj, selLTLT, NULL, 0, 1, other);
}

inline VALUE
vm_when_splat(void *cache, unsigned char overriden,
	VALUE comparedTo, VALUE splat)
{
    VALUE ary = rb_check_convert_type(splat, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, splat);
    }
    long i, count = RARRAY_LEN(ary);
    for (i = 0; i < count; i++) {
	VALUE o = RARRAY_AT(ary, i);
	if (RTEST(vm_fast_eqq(cache, o, comparedTo, overriden))) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

inline void
vm_set_current_scope(VALUE mod, int scope)
{
    rb_vm_set_current_scope(mod, scope);
}

inline VALUE
vm_ocval_to_rval(void *ocval)
{
    return OC2RB(ocval);
}

inline VALUE
vm_char_to_rval(char c)
{
    return INT2FIX(c);
}

inline VALUE
vm_uchar_to_rval(unsigned char c)
{
    return INT2FIX(c);
}

inline VALUE
vm_short_to_rval(short c)
{
    return INT2FIX(c);
}

inline VALUE
vm_ushort_to_rval(unsigned short c)
{
    return INT2FIX(c);
}

inline VALUE
vm_int_to_rval(int c)
{
    return INT2FIX(c);
}

inline VALUE
vm_uint_to_rval(unsigned int c)
{
    return INT2FIX(c);
}

inline VALUE
vm_long_to_rval(long l)
{
    return LONG2NUM(l);
}

inline VALUE
vm_ulong_to_rval(unsigned long l)
{
    return ULONG2NUM(l);
}

inline VALUE
vm_long_long_to_rval(long long l)
{
    return LL2NUM(l);
}

inline VALUE
vm_ulong_long_to_rval(unsigned long long l)
{
    return ULL2NUM(l);
}

inline VALUE
vm_float_to_rval(float f)
{
    return DOUBLE2NUM(f);
}

inline VALUE
vm_double_to_rval(double d)
{
    return DOUBLE2NUM(d);
}

inline VALUE
vm_sel_to_rval(SEL sel)
{
    return sel == 0 ? Qnil : ID2SYM(rb_intern(sel_getName(sel)));
}

inline VALUE
vm_charptr_to_rval(const char *ptr)
{
    return ptr == NULL ? Qnil : rb_str_new2(ptr);
}

inline void
vm_rval_to_ocval(VALUE rval, void **ocval)
{
    *ocval = rval == Qnil ? NULL : RB2OC(rval);
}

inline void
vm_rval_to_bool(VALUE rval, BOOL *ocval)
{
    if (rval == Qfalse || rval == Qnil) {
	*ocval = NO;
    }
    else {
	// All other types should be converted as true, to follow the Ruby
	// semantics (where for example any integer is always true, even 0).
	*ocval = YES;
    }
}

static inline const char *
rval_to_c_str(VALUE rval)
{
    if (NIL_P(rval)) {
	return NULL;
    }
    else {
	if (CLASS_OF(rval) == rb_cSymbol) {
	    return rb_sym2name(rval);
	}
	if (rb_obj_is_kind_of(rval, rb_cPointer)) {
	    return (const char *)rb_pointer_get_data(rval, "^c");
	}
	return StringValueCStr(rval);
    }
}

inline void
vm_rval_to_sel(VALUE rval, SEL *ocval)
{
    const char *cstr = rval_to_c_str(rval);
    *ocval = cstr == NULL ? NULL : sel_registerName(cstr);
}

inline void
vm_rval_to_charptr(VALUE rval, const char **ocval)
{
    *ocval = rval_to_c_str(rval);
}

static inline long
bool_to_fix(VALUE rval)
{
    if (rval == Qtrue) {
	return INT2FIX(1);
    }
    if (rval == Qfalse) {
	return INT2FIX(0);
    }
    return rval;
}

static inline long
rval_to_long(VALUE rval)
{
   return NUM2LONG(rb_Integer(bool_to_fix(rval))); 
}

static inline long long
rval_to_long_long(VALUE rval)
{
    return NUM2LL(rb_Integer(bool_to_fix(rval)));
}

static inline double
rval_to_double(VALUE rval)
{
    return RFLOAT_VALUE(rb_Float(bool_to_fix(rval)));
}

inline void
vm_rval_to_char(VALUE rval, char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (char)rval_to_long(rval);
    }
}

inline void
vm_rval_to_uchar(VALUE rval, unsigned char *ocval)
{
    if (TYPE(rval) == T_STRING && RSTRING_LEN(rval) == 1) {
	*ocval = (unsigned char)RSTRING_PTR(rval)[0];
    }
    else {
	*ocval = (unsigned char)rval_to_long(rval);
    }
}

inline void
vm_rval_to_short(VALUE rval, short *ocval)
{
    *ocval = (short)rval_to_long(rval);
}

inline void
vm_rval_to_ushort(VALUE rval, unsigned short *ocval)
{
    *ocval = (unsigned short)rval_to_long(rval);
}

inline void
vm_rval_to_int(VALUE rval, int *ocval)
{
    *ocval = (int)rval_to_long(rval);
}

inline void
vm_rval_to_uint(VALUE rval, unsigned int *ocval)
{
    *ocval = (unsigned int)rval_to_long(rval);
}

inline void
vm_rval_to_long(VALUE rval, long *ocval)
{
    *ocval = (long)rval_to_long(rval);
}

inline void
vm_rval_to_ulong(VALUE rval, unsigned long *ocval)
{
    *ocval = (unsigned long)rval_to_long(rval);
}

inline void
vm_rval_to_long_long(VALUE rval, long long *ocval)
{
    *ocval = (long long)rval_to_long_long(rval);
}

inline void
vm_rval_to_ulong_long(VALUE rval, unsigned long long *ocval)
{
    *ocval = (unsigned long long)rval_to_long_long(rval);
}

inline void
vm_rval_to_double(VALUE rval, double *ocval)
{
    *ocval = (double)rval_to_double(rval);
}

inline void
vm_rval_to_float(VALUE rval, float *ocval)
{
    *ocval = (float)rval_to_double(rval);
}

inline void *
vm_rval_to_cptr(VALUE rval, const char *type, void **cptr)
{
    if (NIL_P(rval)) {
	*cptr = NULL;
    }
    else {
	if (TYPE(rval) == T_ARRAY
	    || rb_boxed_is_type(CLASS_OF(rval), type + 1)) {
	    // A convenience helper so that the user can pass a Boxed or an 
	    // Array object instead of a Pointer to the object.
	    rval = rb_pointer_new2(type + 1, rval);
	}
	*cptr = rb_pointer_get_data(rval, type);
    }
    return *cptr;
}

inline VALUE
vm_to_a(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, obj);
    }
    return ary;
}

inline VALUE
vm_to_ary(VALUE obj)
{
    VALUE ary = rb_check_convert_type(obj, T_ARRAY, "Array", "to_ary");
    if (NIL_P(ary)) {
	ary = rb_ary_new3(1, obj);
    }
    return ary;
}

inline VALUE
vm_ary_cat(VALUE ary, VALUE obj)
{
    VALUE ary2 = rb_check_convert_type(obj, T_ARRAY, "Array", "to_a");
    if (!NIL_P(ary2)) {
	rb_ary_concat(ary, ary2);
    }
    else {
	rb_ary_push(ary, obj);
    }
    return ary;
}

inline VALUE
vm_ary_dup(VALUE ary)
{
    return rb_ary_dup(ary);
}

inline VALUE
vm_rary_new(int len)
{
    VALUE ary = rb_ary_new2(len);
    RARY(ary)->len = len;
    return ary;
}

inline void
vm_rary_aset(VALUE ary, int i, VALUE obj)
{
    rary_elt_set(ary, i, obj);
}

inline VALUE
vm_rhash_new(void)
{
    return rb_hash_new();
}

inline void
vm_rhash_store(VALUE hash, VALUE key, VALUE obj)
{
    rhash_store(hash, key, obj);
}

inline VALUE
vm_masgn_get_elem_before_splat(VALUE ary, int offset)
{
    if (offset < RARRAY_LEN(ary)) {
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

inline VALUE
vm_masgn_get_elem_after_splat(VALUE ary, int before_splat_count,
	int after_splat_count, int offset)
{
    const int len = RARRAY_LEN(ary);
    if (len < before_splat_count + after_splat_count) {
	offset += before_splat_count;
	if (offset < len) {
	    return RARRAY_AT(ary, offset);
	}
    }
    else {
	offset += len - after_splat_count;
	return RARRAY_AT(ary, offset);
    }
    return Qnil;
}

inline VALUE
vm_masgn_get_splat(VALUE ary, int before_splat_count, int after_splat_count)
{
    const int len = RARRAY_LEN(ary);
    if (len > before_splat_count + after_splat_count) {
	return rb_ary_subseq(ary, before_splat_count,
		len - before_splat_count - after_splat_count);
    }
    else {
	return rb_ary_new();
    }
}

inline VALUE
vm_get_special(char code)
{
    VALUE backref = rb_backref_get();
    if (backref == Qnil) {
	return Qnil;
    }

    VALUE val;
    switch (code) {
	case '&':
	    val = rb_reg_last_match(backref);
	    break;
	case '`':
	    val = rb_reg_match_pre(backref);
	    break;
	case '\'':
	    val = rb_reg_match_post(backref);
	    break;
	case '+':
	    val = rb_reg_match_last(backref);
	    break;
	default:
	    // Boundaries check is done in rb_reg_nth_match().
	    val = rb_reg_nth_match((int)code, backref);
	    break;
    }
    return val;
}
