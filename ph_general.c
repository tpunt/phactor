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

#include <stdlib.h>
#include <string.h>

#include "ph_general.h"

ph_string_t *ph_string_new(char *s, int len)
{
    ph_string_t *phstr = malloc(sizeof(ph_string_t));

    PH_STRL_P(phstr) = len;
    PH_STRV_P(phstr) = malloc(sizeof(char) * len);
    memcpy(PH_STRV_P(phstr), s, len);

    return phstr;
}

void ph_string_update(ph_string_t *phstr, char *s, int len)
{
    PH_STRL_P(phstr) = len;
    PH_STRV_P(phstr) = malloc(sizeof(char) * len);
    memcpy(PH_STRV_P(phstr), s, len);
}

int ph_str_eq(ph_string_t *phstr1, ph_string_t *phstr2)
{
    return PH_STRL_P(phstr1) == PH_STRL_P(phstr2) && !strncmp(PH_STRV_P(phstr1), PH_STRV_P(phstr2), PH_STRL_P(phstr2));
}
