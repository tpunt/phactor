/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-present The PHP Group                             |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Thomas Punt <tpunt@php.net>                                  |
  +----------------------------------------------------------------------+
*/

#ifndef PH_STRING_H
#define PH_STRING_H

#define PH_STRL(s) (s).len
#define PH_STRV(s) (s).val
#define PH_STRL_P(s) PH_STRL(*(s))
#define PH_STRV_P(s) PH_STRV(*(s))

typedef struct _ph_string_t {
    int len;
    char *val;
} ph_string_t;

ph_string_t *ph_str_alloc(int len);
ph_string_t *ph_str_create(char *s, int len);
void ph_str_set(ph_string_t *phstr, char *s, int len);
int ph_str_eq(ph_string_t *phstr1, ph_string_t *phstr2);
void ph_str_value_free(ph_string_t *phstr);
void ph_str_free(ph_string_t *phstr);

#endif
