/*
 * Copyright © 2017 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-str.h"

#include <errno.h>
#include <string.h>

typedef void (* zero_fn_t) (void  *s,
                            size_t n);
void
bolt_erase_n (void *data, gsize n)
{
#if !HAVE_FN_EXPLICIT_BZERO
  #warning no explicit bzero, using fallback
  static volatile zero_fn_t explicit_bzero = bzero;
#endif

  explicit_bzero (data, n);
}

void
bolt_str_erase (char *str)
{
  if (str == NULL)
    return;

  bolt_erase_n (str, strlen (str));
}

void
bolt_str_erase_clear (char **str)
{
  g_return_if_fail (str != NULL);
  if (*str == NULL)
    return;

  bolt_str_erase (*str);
  g_free (*str);
  *str = NULL;
}

GStrv
bolt_strv_from_ptr_array (GPtrArray **array)
{
  GPtrArray *a;

  if (array == NULL || *array == NULL)
    return NULL;

  a = *array;

  if (a->len == 0 || a->pdata[a->len - 1] != NULL)
    g_ptr_array_add (a, NULL);

  *array = NULL;
  return (GStrv) g_ptr_array_free (a, FALSE);
}

gsize
bolt_strv_length (char * const *strv)
{
  gsize l = 0;

  if (strv == NULL)
    return 0;

  while (*strv++ != NULL)
    l++;

  return l;
}

char **
bolt_strv_contains (GStrv haystack, const char *needle)
{
  g_return_val_if_fail (needle != NULL, NULL);

  if (haystack == NULL)
    return NULL;

  for (char **iter = haystack; *iter != NULL; iter++)
    if (bolt_streq (*iter, needle))
      return iter;

  return NULL;
}

gboolean
bolt_strv_equal (const GStrv a, const GStrv b)
{
  guint na, nb;

  if (a == b)
    return TRUE;

  na = a == NULL ? 0 : g_strv_length (a);
  nb = b == NULL ? 0 : g_strv_length (b);

  if (na != nb)
    return FALSE;

  for (guint i = 0; i < na; i++)
    if (!bolt_streq (a[i], b[i]))
      return FALSE;

  return TRUE;
}

gboolean
bolt_strv_diff (const GStrv before,
                const GStrv after,
                GStrv      *added,
                GStrv      *removed)
{
  g_autoptr(GHashTable) idx = NULL;
  g_autoptr(GPtrArray) add = NULL;
  g_autoptr(GPtrArray) rem = NULL;
  GHashTableIter hi;
  gboolean changed;
  gpointer k, v;

  idx = g_hash_table_new (g_str_hash, g_str_equal);
  add = g_ptr_array_new ();
  rem = g_ptr_array_new ();

  /* build an index of everything in 'before', a value of '0'
   * means unseen and a value of '1' means seen
   */
  for (char **iter = before; iter && *iter; iter++)
    g_hash_table_insert (idx, *iter, GUINT_TO_POINTER ('0'));

  for (char **iter = after; iter && *iter; iter++)
    {
      gpointer val = g_hash_table_lookup (idx, *iter);

      /* if the given string is not in the index, it is new;
       * else, i.e if it is present, mark it as seen ('1').
       */
      if (val == NULL)
        g_ptr_array_add (add, g_strdup (*iter));
      else
        g_hash_table_replace (idx, *iter, GUINT_TO_POINTER ('1'));
    }

  /* scan the hashtable, all unseen (i.e. unmarked) entries
   * are not in 'after' and thus must have been removed
   */
  g_hash_table_iter_init (&hi, idx);
  while (g_hash_table_iter_next (&hi, &k, &v))
    if (GPOINTER_TO_UINT (v) != '1')
      g_ptr_array_add (rem, g_strdup ((char *) k));

  changed = add->len > 0 || rem->len > 0;

  g_ptr_array_sort (add, bolt_comparefn_strcmp);
  g_ptr_array_sort (rem, bolt_comparefn_strcmp);

  g_ptr_array_add (add, NULL);
  g_ptr_array_add (rem, NULL);

  if (added)
    *added = (GStrv) g_ptr_array_free (g_steal_pointer (&add), FALSE);

  if (removed)
    *removed = (GStrv) g_ptr_array_free (g_steal_pointer (&rem), FALSE);

  return changed;
}

char **
bolt_strv_rotate_left (char **strv)
{
  char *start;
  char **prev;
  char **iter;

  if (strv == NULL || *strv == NULL)
    return NULL;

  start = *strv; /* remember the first element */
  for (prev = strv, iter = strv + 1; *iter; prev = iter, iter++)
    *prev = *iter;

  *prev = start;
  return prev;
}

char *
bolt_strdup_validate (const char *string)
{
  g_autofree char *str = NULL;
  gboolean ok;
  gsize l;

  if (string == NULL)
    return NULL;

  str = g_strdup (string);
  str = g_strstrip (str);

  l = strlen (str);
  if (l == 0)
    return NULL;

  ok = g_utf8_validate (str, l, NULL);

  if (!ok)
    return NULL;

  return g_steal_pointer (&str);
}

char *
bolt_strstrip (char *string)
{
  char *str;

  if (string == NULL)
    return NULL;

  str = g_strstrip (string);

  if (strlen (str) == 0)
    g_clear_pointer (&str, g_free);

  return str;
}

gboolean
bolt_str_parse_as_int (const char *str,
                       gint       *ret)
{
  char *end;
  gint64 val;

  g_return_val_if_fail (str != NULL, -1);

  errno = 0;
  val = g_ascii_strtoll (str, &end, 0);

  /* conversion errors */
  if (val == 0 && errno != 0)
    return FALSE;

  /* check over/underflow */
  if ((val == G_MAXINT64 || val == G_MININT64) &&
      errno == ERANGE)
    return FALSE;

  if (str == end)
    {
      errno = -EINVAL;
      return FALSE;
    }

  if (val > G_MAXINT || val < G_MININT)
    {
      errno = -ERANGE;
      return FALSE;
    }

  if (ret)
    *ret = (gint) val;

  return TRUE;
}

gint
bolt_comparefn_strcmp (gconstpointer a,
                       gconstpointer b)
{
  const char * const *astrv = a;
  const char * const *bstrv = b;

  return g_strcmp0 (*astrv, *bstrv);
}
