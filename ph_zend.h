#ifndef PH_ZEND_H
#define PH_ZEND_H

ZEND_API HashTable* ZEND_FASTCALL ph_zend_array_dup(HashTable *source);
ZEND_API zend_ast *ph_zend_ast_copy(zend_ast *ast);

#endif
