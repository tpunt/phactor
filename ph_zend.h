#ifndef PH_ZEND_H
#define PH_ZEND_H

ZEND_API HashTable* ZEND_FASTCALL ph_zend_array_dup(HashTable *source);
ZEND_API zend_ast *ph_zend_ast_copy(zend_ast *ast);
ZEND_API void ZEND_FASTCALL _ph_zval_copy_ctor_func(zval *zvalue ZEND_FILE_LINE_DC);

/*
A thread-safe version of ZVAL_DUP, where constant AST and array types are
properly copied for threaded environments.
*/
#define PH_ZVAL_DUP(z, v)									\
	do {												\
		zval *_z1 = (z);								\
		const zval *_z2 = (v);							\
		zend_refcounted *_gc = Z_COUNTED_P(_z2);		\
		uint32_t _t = Z_TYPE_INFO_P(_z2);				\
		ZVAL_COPY_VALUE_EX(_z1, _z2, _gc, _t);			\
		if ((_t & ((IS_TYPE_REFCOUNTED|IS_TYPE_COPYABLE) << Z_TYPE_FLAGS_SHIFT)) != 0) { \
			if ((_t & (IS_TYPE_COPYABLE << Z_TYPE_FLAGS_SHIFT)) != 0) { \
				_ph_zval_copy_ctor_func(_z1 ZEND_FILE_LINE_CC); \
			} else {									\
				GC_REFCOUNT(_gc)++;						\
			}											\
		}												\
	} while (0)

#endif
