/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2016 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author:                                                              |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php_phactor.h"
#include "ph_copy.h"

void ph_store_add(store_t *store, zend_string *name, zval *value, uint32_t scope)
{
    ph_string_t key;

    PH_STRL(key) = ZSTR_LEN(name);
    PH_STRV(key) = ZSTR_VAL(name);

    entry_t *entry = ph_hashtable_search(&store->props, &key);

    if (entry) {
        ph_entry_update(entry, value);
    } else {
        ph_hashtable_insert(&store->props, ph_string_new(ZSTR_VAL(name), ZSTR_LEN(name)), create_new_entry(value, scope));
    }
}

void ph_store_to_hashtable(HashTable *ht, store_t *store)
{
    ph_hashtable_to_hashtable(ht, &store->props);
}

void ph_store_read(store_t *store, zend_string *key, zval *rv, zval *this)
{
    ph_string_t phstr;

    PH_STRV(phstr) = ZSTR_VAL(key);
    PH_STRL(phstr) = ZSTR_LEN(key);

    entry_t *e = ph_hashtable_search(&store->props, &phstr);

    if (!e) {
        zend_throw_error(zend_ce_type_error, "Cannot read property '%s' becaused it is undefined\n", ZSTR_VAL(key));
        return;
    }

    switch (ENTRY_SCOPE(e)) {
        case ZEND_ACC_SHADOW:
            zend_throw_error(zend_ce_type_error, "Cannot read property '%s' becaused it is private\n", ZSTR_VAL(key));
            break;
        case ZEND_ACC_PRIVATE | ZEND_ACC_CHANGED:
        case ZEND_ACC_PRIVATE:
            if (!this || Z_OBJCE_P(this) != store->ce) {
                zend_throw_error(zend_ce_type_error, "Cannot read property '%s' becaused it is private\n", ZSTR_VAL(key));
            } else {
                ph_convert_entry_to_zval(rv, e);
            }
            break;
        case ZEND_ACC_PROTECTED:
            if (!this || !instanceof_function(Z_OBJCE_P(this), store->ce)) {
                zend_throw_error(zend_ce_type_error, "Cannot read property '%s' becaused it is protected\n", ZSTR_VAL(key));
            } else {
                ph_convert_entry_to_zval(rv, e);
            }
            break;
        case ZEND_ACC_PUBLIC:
            ph_convert_entry_to_zval(rv, e);
            break;
        default:
            php_printf("Unknown scope used (%d)\n", ENTRY_SCOPE(e));
            assert(0);
    }
}

void ph_entry_delete(void *entry_void)
{
    entry_t *entry = entry_void;

    ph_entry_delete_value(entry);

    free(entry);
}

void ph_entry_delete_value(entry_t *entry)
{
    switch (ENTRY_TYPE(entry)) {
        case PH_STORE_FUNC:
            free(ENTRY_FUNC(entry));
            break;
        case IS_ARRAY:
        case IS_STRING:
            free(PH_STRV(ENTRY_STRING(entry)));
    }
}

void ph_convert_entry_to_zval(zval *value, entry_t *e)
{
    switch (ENTRY_TYPE(e)) {
        case IS_STRING:
            ZVAL_STR(value, zend_string_init(PH_STRV(ENTRY_STRING(e)), PH_STRL(ENTRY_STRING(e)), 0));
            break;
        case IS_LONG:
            ZVAL_LONG(value, ENTRY_LONG(e));
            break;
        case IS_DOUBLE:
            ZVAL_DOUBLE(value, ENTRY_DOUBLE(e));
            break;
        case _IS_BOOL:
            ZVAL_BOOL(value, ENTRY_BOOL(e));
            break;
        case IS_NULL:
            ZVAL_NULL(value);
            break;
        case IS_ARRAY:
            {
                size_t buf_len = PH_STRL(ENTRY_STRING(e));
                const unsigned char *p = (const unsigned char *) PH_STRV(ENTRY_STRING(e));
                php_unserialize_data_t var_hash;

                PHP_VAR_UNSERIALIZE_INIT(var_hash);

                if (!php_var_unserialize(value, &p, p + buf_len, &var_hash)) {
                    // @todo handle serialisation failure - is this even possible to hit?
                }

                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
            }
            break;
        case PH_STORE_FUNC:
            {
                zend_function *closure = copy_user_function(ENTRY_FUNC(e), NULL);
                char *name;
                size_t name_len;

                zend_create_closure(value, closure, zend_get_executed_scope(), closure->common.scope, NULL);
                name_len = spprintf(&name, 0, "Closure@%p", zend_get_closure_method_def(value));

                if (!zend_hash_str_update_ptr(EG(function_table), name, name_len, closure)) {
                    printf("FAILED!\n");
                }

                efree(name);
            }
            break;
        case PH_STORE_ACTOR:
            ZVAL_OBJ(value, &ENTRY_ACTOR(e)->obj);
            // @todo obj->ce will need updating - how to ensure that it is correct
            // depending upon its usage context? E.g. For property reads, it can
            // simply be updated here, but what about for message sending?

            // manually increment to prevent refcount from hitting 0 (and therefore
            // trying to be freed by the GC)
            ++GC_REFCOUNT(&ENTRY_ACTOR(e)->obj);
            break;
        case IS_OBJECT:
            {
                size_t buf_len = PH_STRL(ENTRY_STRING(e));
                const unsigned char *p = (const unsigned char *) PH_STRV(ENTRY_STRING(e));
                php_unserialize_data_t var_hash;

                PHP_VAR_UNSERIALIZE_INIT(var_hash);

                if (!php_var_unserialize(value, &p, p + buf_len, &var_hash)) {
                    // @todo handle serialisation failure - is this even possible to hit?
                }

                PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
            }
    }
}

void ph_convert_zval_to_entry(entry_t *e, zval *value)
{
    ENTRY_TYPE(e) = Z_TYPE_P(value);

    switch (Z_TYPE_P(value)) {
        case IS_STRING:
            PH_STRL(ENTRY_STRING(e)) = ZSTR_LEN(Z_STR_P(value));
            PH_STRV(ENTRY_STRING(e)) = malloc(sizeof(char) * PH_STRL(ENTRY_STRING(e)));
            memcpy(PH_STRV(ENTRY_STRING(e)), ZSTR_VAL(Z_STR_P(value)), sizeof(char) * PH_STRL(ENTRY_STRING(e)));
            break;
        case IS_LONG:
            ENTRY_LONG(e) = Z_LVAL_P(value);
            break;
        case IS_DOUBLE:
            ENTRY_DOUBLE(e) = Z_DVAL_P(value);
            break;
        case _IS_BOOL:
            ENTRY_BOOL(e) = !!Z_LVAL_P(value);
            break;
        case IS_ARRAY:
            {
                smart_str smart = {0};
                php_serialize_data_t vars;

                PHP_VAR_SERIALIZE_INIT(vars);
                php_var_serialize(&smart, value, &vars);
                PHP_VAR_SERIALIZE_DESTROY(vars);

                if (EG(exception)) {
                    smart_str_free(&smart);
                } else {
                    zend_string *sval = smart_str_extract(&smart);

                    PH_STRL(ENTRY_STRING(e)) = ZSTR_LEN(sval);
                    PH_STRV(ENTRY_STRING(e)) = malloc(ZSTR_LEN(sval));
                    memcpy(PH_STRV(ENTRY_STRING(e)), ZSTR_VAL(sval), ZSTR_LEN(sval));

                    zend_string_free(sval);
                }
            }
            break;
        case IS_OBJECT:
            {
                if (instanceof_function(Z_OBJCE_P(value), zend_ce_closure)) {
                    ENTRY_TYPE(e) = PH_STORE_FUNC;
                    ENTRY_FUNC(e) = malloc(sizeof(zend_op_array));
                    memcpy(ENTRY_FUNC(e), zend_get_closure_method_def(value), sizeof(zend_op_array));
                    Z_ADDREF_P(value);
                } else if (instanceof_function(Z_OBJCE_P(value), Actor_ce)) {
                    ENTRY_ACTOR(e) = (actor_t *)((char *)Z_OBJ_P(value) - Z_OBJ_HANDLER_P(value, offset));
                    ENTRY_TYPE(e) = PH_STORE_ACTOR;
                } else {
                    // temporary solution - just serialise it and to the hell with the consequences
                    smart_str smart = {0};
                    php_serialize_data_t vars;

                    PHP_VAR_SERIALIZE_INIT(vars);
                    php_var_serialize(&smart, value, &vars);
                    PHP_VAR_SERIALIZE_DESTROY(vars);

                    if (EG(exception)) {
                        smart_str_free(&smart);
                    } else {
                        zend_string *sval = smart_str_extract(&smart);

                        PH_STRL(ENTRY_STRING(e)) = ZSTR_LEN(sval);
                        PH_STRV(ENTRY_STRING(e)) = malloc(ZSTR_LEN(sval));
                        memcpy(PH_STRV(ENTRY_STRING(e)), ZSTR_VAL(sval), ZSTR_LEN(sval));

                        zend_string_free(sval);
                    }
                }
            }
    }
}

void ph_entry_update(entry_t *entry, zval *value)
{
    ph_entry_delete_value(entry);
    ph_convert_zval_to_entry(entry, value);
}

entry_t *create_new_entry(zval *value, uint32_t scope)
{
    entry_t *e = malloc(sizeof(entry_t));

    ENTRY_SCOPE(e) = scope;

    ph_convert_zval_to_entry(e, value);

    return e;
}
