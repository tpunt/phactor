PHP_ARG_ENABLE(phactor, whether to enable phactor support,
[  --enable-phactor           Enable phactor support])

if test "$PHP_PHACTOR" != "no"; then
    PHP_NEW_EXTENSION(phactor, phactor.c \
        src/ph_copy.c \
        src/ph_debug.c \
        src/ph_entry.c \
        src/ph_string.c \
        src/ph_context.c \
        src/ph_context_switch.S \
        src/ph_zend.c \
        src/ph_task.c \
        src/ph_message.c \
        src/ds/ph_queue.c \
        src/ds/ph_vector.c \
        src/ds/ph_hashtable.c \
        src/classes/actor_system.c \
        src/classes/actor.c \
        src/classes/actor_ref.c \
        src/classes/supervisor.c, $ext_shared,, -DZEND_ENABLE_STATIC_TSRMLS_CACHE=1)

    EXTRA_CFLAGS="$EXTRA_CFLAGS -std=gnu99"
    PHP_SUBST(EXTRA_CFLAGS)
fi
