PHP_ARG_ENABLE(phactor, whether to enable phactor support,
[  --enable-phactor           Enable phactor support])

if test "$PHP_PHACTOR" != "no"; then
  PHP_NEW_EXTENSION(phactor, phactor.c ph_copy.c ph_debug.c ph_hashtable.c ph_prop_store.c ph_general.c ph_context.c ph_context_switch.s, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)
fi
