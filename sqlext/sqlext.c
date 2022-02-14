/*
 * Copyright (C) 2009-2010 Julien BLACHE <jb@jblache.org>
 * Copyright (C) 2010 Kai Elwert <elwertk@googlemail.com>
 * Copyright (C) 2022 Espen Jürgensen <espenjurgensen@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
  How to test and debug
  ---------------------
  1. Build extension with debug flag
     gcc -Wall -g -fPIC -shared sqlext.c -o sqlext.so -lunistring
  2. Start sqlite3 in gdb
     gdb --args sqlite3
  3. Optionally add a breakpoint, and then run sqlite3
     b sqlext.c:123
     run
  4. Load extension and run tests
     .load ./sqlext.so
     select '01', like('æ', 'Æ') = 1;
     select '02', like('o', 'Ö') = 1;
     select '03', like('é', 'e') = 1;
     select '04', like('O', 'Ø') = 0;
     select '05', like('%test\%', 'testx', '\') = 0;
     select '06', like('Ö', 'o') = 1;
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <unistr.h>
#include <unictype.h>
#include <unicase.h>

#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1


/* ============ Fast Unicode case folding and diacritics removal ============ */
/*      The code in this section is copied from sqlite's fts5_unicode.c,      */
/*   because it is about 4x faster than using libunistring's u32_casefold()   */

/*
** If the argument is a codepoint corresponding to a lowercase letter
** in the ASCII range with a diacritic added, return the codepoint
** of the ASCII letter only. For example, if passed 235 - "LATIN
** SMALL LETTER E WITH DIAERESIS" - return 65 ("LATIN SMALL LETTER
** E"). The resuls of passing a codepoint that corresponds to an
** uppercase letter are undefined.
*/
static int fts5_remove_diacritic(int c, int bComplex){
  unsigned short aDia[] = {
        0,  1797,  1848,  1859,  1891,  1928,  1940,  1995,
     2024,  2040,  2060,  2110,  2168,  2206,  2264,  2286,
     2344,  2383,  2472,  2488,  2516,  2596,  2668,  2732,
     2782,  2842,  2894,  2954,  2984,  3000,  3028,  3336,
     3456,  3696,  3712,  3728,  3744,  3766,  3832,  3896,
     3912,  3928,  3944,  3968,  4008,  4040,  4056,  4106,
     4138,  4170,  4202,  4234,  4266,  4296,  4312,  4344,
     4408,  4424,  4442,  4472,  4488,  4504,  6148,  6198,
     6264,  6280,  6360,  6429,  6505,  6529, 61448, 61468,
    61512, 61534, 61592, 61610, 61642, 61672, 61688, 61704,
    61726, 61784, 61800, 61816, 61836, 61880, 61896, 61914,
    61948, 61998, 62062, 62122, 62154, 62184, 62200, 62218,
    62252, 62302, 62364, 62410, 62442, 62478, 62536, 62554,
    62584, 62604, 62640, 62648, 62656, 62664, 62730, 62766,
    62830, 62890, 62924, 62974, 63032, 63050, 63082, 63118,
    63182, 63242, 63274, 63310, 63368, 63390,
  };
#define HIBIT ((unsigned char)0x80)
  unsigned char aChar[] = {
    '\0',      'a',       'c',       'e',       'i',       'n',
    'o',       'u',       'y',       'y',       'a',       'c',
    'd',       'e',       'e',       'g',       'h',       'i',
    'j',       'k',       'l',       'n',       'o',       'r',
    's',       't',       'u',       'u',       'w',       'y',
    'z',       'o',       'u',       'a',       'i',       'o',
    'u',       'u'|HIBIT, 'a'|HIBIT, 'g',       'k',       'o',
    'o'|HIBIT, 'j',       'g',       'n',       'a'|HIBIT, 'a',
    'e',       'i',       'o',       'r',       'u',       's',
    't',       'h',       'a',       'e',       'o'|HIBIT, 'o',
    'o'|HIBIT, 'y',       '\0',      '\0',      '\0',      '\0',
    '\0',      '\0',      '\0',      '\0',      'a',       'b',
    'c'|HIBIT, 'd',       'd',       'e'|HIBIT, 'e',       'e'|HIBIT,
    'f',       'g',       'h',       'h',       'i',       'i'|HIBIT,
    'k',       'l',       'l'|HIBIT, 'l',       'm',       'n',
    'o'|HIBIT, 'p',       'r',       'r'|HIBIT, 'r',       's',
    's'|HIBIT, 't',       'u',       'u'|HIBIT, 'v',       'w',
    'w',       'x',       'y',       'z',       'h',       't',
    'w',       'y',       'a',       'a'|HIBIT, 'a'|HIBIT, 'a'|HIBIT,
    'e',       'e'|HIBIT, 'e'|HIBIT, 'i',       'o',       'o'|HIBIT,
    'o'|HIBIT, 'o'|HIBIT, 'u',       'u'|HIBIT, 'u'|HIBIT, 'y',
  };

  unsigned int key = (((unsigned int)c)<<3) | 0x00000007;
  int iRes = 0;
  int iHi = sizeof(aDia)/sizeof(aDia[0]) - 1;
  int iLo = 0;
  while( iHi>=iLo ){
    int iTest = (iHi + iLo) / 2;
    if( key >= aDia[iTest] ){
      iRes = iTest;
      iLo = iTest+1;
    }else{
      iHi = iTest-1;
    }
  }
  assert( key>=aDia[iRes] );
  if( bComplex==0 && (aChar[iRes] & 0x80) ) return c;
  return (c > (aDia[iRes]>>3) + (aDia[iRes]&0x07)) ? c : ((int)aChar[iRes] & 0x7F);
}

/*
** Interpret the argument as a unicode codepoint. If the codepoint
** is an upper case character that has a lower case equivalent,
** return the codepoint corresponding to the lower case version.
** Otherwise, return a copy of the argument.
**
** The results are undefined if the value passed to this function
** is less than zero.
*/
static int sqlite3Fts5UnicodeFold(int c, int eRemoveDiacritic){
  /* Each entry in the following array defines a rule for folding a range
  ** of codepoints to lower case. The rule applies to a range of nRange
  ** codepoints starting at codepoint iCode.
  **
  ** If the least significant bit in flags is clear, then the rule applies
  ** to all nRange codepoints (i.e. all nRange codepoints are upper case and
  ** need to be folded). Or, if it is set, then the rule only applies to
  ** every second codepoint in the range, starting with codepoint C.
  **
  ** The 7 most significant bits in flags are an index into the aiOff[]
  ** array. If a specific codepoint C does require folding, then its lower
  ** case equivalent is ((C + aiOff[flags>>1]) & 0xFFFF).
  **
  ** The contents of this array are generated by parsing the CaseFolding.txt
  ** file distributed as part of the "Unicode Character Database". See
  ** http://www.unicode.org for details.
  */
  static const struct TableEntry {
    unsigned short iCode;
    unsigned char flags;
    unsigned char nRange;
  } aEntry[] = {
    {65, 14, 26},          {181, 64, 1},          {192, 14, 23},
    {216, 14, 7},          {256, 1, 48},          {306, 1, 6},
    {313, 1, 16},          {330, 1, 46},          {376, 116, 1},
    {377, 1, 6},           {383, 104, 1},         {385, 50, 1},
    {386, 1, 4},           {390, 44, 1},          {391, 0, 1},
    {393, 42, 2},          {395, 0, 1},           {398, 32, 1},
    {399, 38, 1},          {400, 40, 1},          {401, 0, 1},
    {403, 42, 1},          {404, 46, 1},          {406, 52, 1},
    {407, 48, 1},          {408, 0, 1},           {412, 52, 1},
    {413, 54, 1},          {415, 56, 1},          {416, 1, 6},
    {422, 60, 1},          {423, 0, 1},           {425, 60, 1},
    {428, 0, 1},           {430, 60, 1},          {431, 0, 1},
    {433, 58, 2},          {435, 1, 4},           {439, 62, 1},
    {440, 0, 1},           {444, 0, 1},           {452, 2, 1},
    {453, 0, 1},           {455, 2, 1},           {456, 0, 1},
    {458, 2, 1},           {459, 1, 18},          {478, 1, 18},
    {497, 2, 1},           {498, 1, 4},           {502, 122, 1},
    {503, 134, 1},         {504, 1, 40},          {544, 110, 1},
    {546, 1, 18},          {570, 70, 1},          {571, 0, 1},
    {573, 108, 1},         {574, 68, 1},          {577, 0, 1},
    {579, 106, 1},         {580, 28, 1},          {581, 30, 1},
    {582, 1, 10},          {837, 36, 1},          {880, 1, 4},
    {886, 0, 1},           {902, 18, 1},          {904, 16, 3},
    {908, 26, 1},          {910, 24, 2},          {913, 14, 17},
    {931, 14, 9},          {962, 0, 1},           {975, 4, 1},
    {976, 140, 1},         {977, 142, 1},         {981, 146, 1},
    {982, 144, 1},         {984, 1, 24},          {1008, 136, 1},
    {1009, 138, 1},        {1012, 130, 1},        {1013, 128, 1},
    {1015, 0, 1},          {1017, 152, 1},        {1018, 0, 1},
    {1021, 110, 3},        {1024, 34, 16},        {1040, 14, 32},
    {1120, 1, 34},         {1162, 1, 54},         {1216, 6, 1},
    {1217, 1, 14},         {1232, 1, 88},         {1329, 22, 38},
    {4256, 66, 38},        {4295, 66, 1},         {4301, 66, 1},
    {7680, 1, 150},        {7835, 132, 1},        {7838, 96, 1},
    {7840, 1, 96},         {7944, 150, 8},        {7960, 150, 6},
    {7976, 150, 8},        {7992, 150, 8},        {8008, 150, 6},
    {8025, 151, 8},        {8040, 150, 8},        {8072, 150, 8},
    {8088, 150, 8},        {8104, 150, 8},        {8120, 150, 2},
    {8122, 126, 2},        {8124, 148, 1},        {8126, 100, 1},
    {8136, 124, 4},        {8140, 148, 1},        {8152, 150, 2},
    {8154, 120, 2},        {8168, 150, 2},        {8170, 118, 2},
    {8172, 152, 1},        {8184, 112, 2},        {8186, 114, 2},
    {8188, 148, 1},        {8486, 98, 1},         {8490, 92, 1},
    {8491, 94, 1},         {8498, 12, 1},         {8544, 8, 16},
    {8579, 0, 1},          {9398, 10, 26},        {11264, 22, 47},
    {11360, 0, 1},         {11362, 88, 1},        {11363, 102, 1},
    {11364, 90, 1},        {11367, 1, 6},         {11373, 84, 1},
    {11374, 86, 1},        {11375, 80, 1},        {11376, 82, 1},
    {11378, 0, 1},         {11381, 0, 1},         {11390, 78, 2},
    {11392, 1, 100},       {11499, 1, 4},         {11506, 0, 1},
    {42560, 1, 46},        {42624, 1, 24},        {42786, 1, 14},
    {42802, 1, 62},        {42873, 1, 4},         {42877, 76, 1},
    {42878, 1, 10},        {42891, 0, 1},         {42893, 74, 1},
    {42896, 1, 4},         {42912, 1, 10},        {42922, 72, 1},
    {65313, 14, 26},
  };
  static const unsigned short aiOff[] = {
   1,     2,     8,     15,    16,    26,    28,    32,
   37,    38,    40,    48,    63,    64,    69,    71,
   79,    80,    116,   202,   203,   205,   206,   207,
   209,   210,   211,   213,   214,   217,   218,   219,
   775,   7264,  10792, 10795, 23228, 23256, 30204, 54721,
   54753, 54754, 54756, 54787, 54793, 54809, 57153, 57274,
   57921, 58019, 58363, 61722, 65268, 65341, 65373, 65406,
   65408, 65410, 65415, 65424, 65436, 65439, 65450, 65462,
   65472, 65476, 65478, 65480, 65482, 65488, 65506, 65511,
   65514, 65521, 65527, 65528, 65529,
  };

  int ret = c;

  assert( sizeof(unsigned short)==2 && sizeof(unsigned char)==1 );

  if( c<128 ){
    if( c>='A' && c<='Z' ) ret = c + ('a' - 'A');
  }else if( c<65536 ){
    const struct TableEntry *p;
    int iHi = sizeof(aEntry)/sizeof(aEntry[0]) - 1;
    int iLo = 0;
    int iRes = -1;

    assert( c>aEntry[0].iCode );
    while( iHi>=iLo ){
      int iTest = (iHi + iLo) / 2;
      int cmp = (c - aEntry[iTest].iCode);
      if( cmp>=0 ){
        iRes = iTest;
        iLo = iTest+1;
      }else{
        iHi = iTest-1;
      }
    }

    assert( iRes>=0 && c>=aEntry[iRes].iCode );
    p = &aEntry[iRes];
    if( c<(p->iCode + p->nRange) && 0==(0x01 & p->flags & (p->iCode ^ c)) ){
      ret = (c + (aiOff[p->flags>>1])) & 0x0000FFFF;
      assert( ret>0 );
    }

    if( eRemoveDiacritic ){
      ret = fts5_remove_diacritic(ret, eRemoveDiacritic==2);
    }
  }

  else if( c>=66560 && c<66600 ){
    ret = c + 40;
  }

  return ret;
}


/* ========================= Custom LIKE function =========================== */
/*   The code in this section is copied from sqlite's icu.c, but instead of   */
/*     libicu it is modified to use the above, plus a bit of libunistring     */

/*
** Maximum length (in bytes) of the pattern in a LIKE or GLOB
** operator.
*/
#ifndef SQLITE_MAX_LIKE_PATTERN_LENGTH
# define SQLITE_MAX_LIKE_PATTERN_LENGTH 50000
#endif

// Not defined in Debian Buster's version of SQLite
#ifdef SQLITE_INNOCUOUS
# define SQLITEICU_EXTRAFLAGS (SQLITE_DETERMINISTIC|SQLITE_INNOCUOUS)
#else
# define SQLITEICU_EXTRAFLAGS SQLITE_DETERMINISTIC
#endif

/*
** This lookup table is used to help decode the first byte of
** a multi-byte UTF8 character. It is copied here from SQLite source
** code file utf8.c.
*/
static const unsigned char icuUtf8Trans1[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x00, 0x01, 0x02, 0x03, 0x00, 0x01, 0x00, 0x00,
};

#define SQLITE_ICU_READ_UTF8(zIn, c)                       \
  c = *(zIn++);                                            \
  if( c>=0xc0 ){                                           \
    c = icuUtf8Trans1[c-0xc0];                             \
    while( (*zIn & 0xc0)==0x80 ){                          \
      c = (c<<6) + (0x3f & *(zIn++));                      \
    }                                                      \
  }

#define SQLITE_ICU_SKIP_UTF8(zIn)                          \
  assert( *zIn );                                          \
  if( *(zIn++)>=0xc0 ){                                    \
    while( (*zIn & 0xc0)==0x80 ){zIn++;}                   \
  }

/*
** Compare two UTF-8 strings for equality where the first string is
** a "LIKE" expression. Return true (1) if they are the same and
** false (0) if they are different.
*/
static int icuLikeCompare(
  const uint8_t *zPattern,   /* LIKE pattern */
  const uint8_t *zString,    /* The UTF-8 string to compare against */
  const uint32_t uEsc        /* The escape character */
){
  static const uint32_t MATCH_ONE = (uint32_t)'_';
  static const uint32_t MATCH_ALL = (uint32_t)'%';

  int prevEscape = 0;     /* True if the previous character was uEsc */

  while( 1 ){

    /* Read (and consume) the next character from the input pattern. */
    uint32_t uPattern;
    SQLITE_ICU_READ_UTF8(zPattern, uPattern);
    if( uPattern==0 ) break;

    /* There are now 4 possibilities:
    **
    **     1. uPattern is an unescaped match-all character "%",
    **     2. uPattern is an unescaped match-one character "_",
    **     3. uPattern is an unescaped escape character, or
    **     4. uPattern is to be handled as an ordinary character
    */
    if( uPattern==MATCH_ALL && !prevEscape && uPattern!=uEsc ){
      /* Case 1. */
      uint8_t c;

      /* Skip any MATCH_ALL or MATCH_ONE characters that follow a
      ** MATCH_ALL. For each MATCH_ONE, skip one character in the
      ** test string.
      */
      while( (c=*zPattern) == MATCH_ALL || c == MATCH_ONE ){
        if( c==MATCH_ONE ){
          if( *zString==0 ) return 0;
          SQLITE_ICU_SKIP_UTF8(zString);
        }
        zPattern++;
      }

      if( *zPattern==0 ) return 1;

      while( *zString ){
        if( icuLikeCompare(zPattern, zString, uEsc) ){
          return 1;
        }
        SQLITE_ICU_SKIP_UTF8(zString);
      }
      return 0;

    }else if( uPattern==MATCH_ONE && !prevEscape && uPattern!=uEsc ){
      /* Case 2. */
      if( *zString==0 ) return 0;
      SQLITE_ICU_SKIP_UTF8(zString);

    }else if( uPattern==uEsc && !prevEscape ){
      /* Case 3. */
      prevEscape = 1;

    }else{
      /* Case 4. */
      uint32_t uString;
      SQLITE_ICU_READ_UTF8(zString, uString);
      if( sqlite3Fts5UnicodeFold(uString, 1) != sqlite3Fts5UnicodeFold(uPattern, 1) )
      {
        return 0;
      }
      prevEscape = 0;
    }
  }

  return *zString==0;
}

/*
** Implementation of the like() SQL function.  This function implements
** the build-in LIKE operator.  The first argument to the function is the
** pattern and the second argument is the string.  So, the SQL statements:
**
**       A LIKE B
**
** is implemented as like(B, A). If there is an escape character E,
**
**       A LIKE B ESCAPE E
**
** is mapped to like(B, A, E).
*/
static void icuLikeFunc(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  const unsigned char *zA = sqlite3_value_text(argv[0]);
  const unsigned char *zB = sqlite3_value_text(argv[1]);
  uint32_t uEsc = 0;

  /* Limit the length of the LIKE or GLOB pattern to avoid problems
  ** of deep recursion and N*N behavior in patternCompare().
  */
  if( sqlite3_value_bytes(argv[0])>SQLITE_MAX_LIKE_PATTERN_LENGTH ){
    sqlite3_result_error(context, "LIKE or GLOB pattern too complex", -1);
    return;
  }


  if( argc==3 ){
    /* The escape character string must consist of a single UTF-8 character.
    ** Otherwise, return an error.
    */
    int nE= sqlite3_value_bytes(argv[2]);
    const unsigned char *zE = sqlite3_value_text(argv[2]);
    if( zE==0 ) return;

    /* Extract first utf-8 character */
    ucs4_t uc;
    if( u8_mbtoucr(&uc, zE, nE) != nE || u32_uctomb(&uEsc, uc, 1) < 0 ){
      sqlite3_result_error(context,
          "ESCAPE expression must be a single character", -1);
      return;
    }
  }

  if( zA && zB ){
    sqlite3_result_int(context, icuLikeCompare(zA, zB, uEsc));
  }
}


/* ================== Other custom functions/collations ===================== */

static void
daap_no_zero_xfunc(sqlite3_context *pv, int n, sqlite3_value **ppv)
{
  sqlite3_int64 new_value;
  sqlite3_int64 old_value;

  if (n != 2)
    {
      sqlite3_result_error(pv, "daap_no_zero() requires 2 parameters, new_value and old_value", -1);
      return;
    }

  if ((sqlite3_value_type(ppv[0]) != SQLITE_INTEGER)
      || (sqlite3_value_type(ppv[1]) != SQLITE_INTEGER))
    {
      sqlite3_result_error(pv, "daap_no_zero() requires 2 integer parameters", -1);
      return;
    }

  new_value = sqlite3_value_int64(ppv[0]);
  old_value = sqlite3_value_int64(ppv[1]);

  if (new_value != 0)
    sqlite3_result_int64(pv, new_value);
  else
    sqlite3_result_int64(pv, old_value);
}

static int
daap_unicode_xcollation(void *notused, int llen, const void *left, int rlen, const void *right)
{
  ucs4_t lch;
  ucs4_t rch;
  int lalpha;
  int ralpha;
  int rpp;
  int ret;

  /* Extract first utf-8 character */
  ret = u8_mbtoucr(&lch, (const uint8_t *)left, llen);
  if (ret < 0)
    return 0;

  ret = u8_mbtoucr(&rch, (const uint8_t *)right, rlen);
  if (ret < 0)
    return 0;

  /* Ensure digits and other non-alphanum sort to tail */
  lalpha = uc_is_alpha(lch);
  ralpha = uc_is_alpha(rch);

  if (!lalpha && ralpha)
    return 1;
  else if (lalpha && !ralpha)
    return -1;

  /* Compare case and normalization insensitive */
  ret = u8_casecmp((const uint8_t *)left, llen, (const uint8_t*)right, rlen, NULL, UNINORM_NFD, &rpp);
  if (ret < 0)
    return 0;

  return rpp;
}

int
sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi)
{
  SQLITE_EXTENSION_INIT2(pApi);
  const char *errmsg = NULL;
  int ret;

  ret = sqlite3_create_function(db, "like", 2, SQLITE_UTF8|SQLITEICU_EXTRAFLAGS, NULL, icuLikeFunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      errmsg = "Could not create custom LIKE function (non-escaped)";
      goto error;
    }

  ret = sqlite3_create_function(db, "like", 3, SQLITE_UTF8|SQLITEICU_EXTRAFLAGS, NULL, icuLikeFunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      errmsg = "Could not create custom LIKE function (escaped)";
      goto error;
    }

  ret = sqlite3_create_function(db, "daap_no_zero", 2, SQLITE_UTF8, NULL, daap_no_zero_xfunc, NULL, NULL);
  if (ret != SQLITE_OK)
    {
      errmsg = "Could not create daap_no_zero function";
      goto error;
    }

  ret = sqlite3_create_collation(db, "DAAP", SQLITE_UTF8, NULL, daap_unicode_xcollation);
  if (ret != SQLITE_OK)
    {
      errmsg = "Could not create sqlite3 custom collation DAAP";
      goto error;
    }

  return 0;

 error:
  if (pzErrMsg)
    *pzErrMsg = sqlite3_mprintf("%s: %s\n", errmsg, sqlite3_errmsg(db));
  return -1;
}
