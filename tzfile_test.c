#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "tzfile.h"

#ifndef TZDIR
#define TZDIR "/usr/share/zoneinfo"
#endif

static dev_t tzfile_dev;
extern char * __tzname[2];
static ino_t tzfile_ino;
static time_t tzfile_mtime;

int __use_tzfile;

/* List of buffers containing time zone strings. */
struct tzstring_l
{
  struct tzstring_l *next;
  size_t len;  /* strlen(data) - doesn't count terminating NUL! */
  char data[0];
};

struct tzstring_l *tzstring_list;

/* Allocate a permanent home for S.  It will never be moved or deallocated,
   but may share space with other strings.
   Don't modify the returned string. */
char *
__tzstring (const char *s)
{
  char *p;
  struct tzstring_l *t, *u, *new;
  size_t len = strlen(s);

  /* Walk the list and look for a match.  If this string is the same
     as the end of an already-allocated string, it can share space. */
  for (u = t = tzstring_list; t; u = t, t = t->next)
    if (len <= t->len)
      {
        p = &t->data[t->len - len];
        if (strcmp (s, p) == 0)
          return p;
      }

  /* Not found; allocate a new buffer. */
  new = malloc (sizeof (struct tzstring_l) + len + 1);
  if (!new)
    return NULL;

  new->next = NULL;
  new->len = len;
  strcpy (new->data, s);

  if (u)
    u->next = new;
  else
    tzstring_list = new;

  return new->data;
}

struct ttinfo
{
    long int offset;            /* Seconds east of GMT.  */
    unsigned char isdst;        /* Used to set tm_isdst.  */
    unsigned char idx;          /* Index into `zone_names'.  */
    unsigned char isstd;        /* Transition times are in standard time.  */
    unsigned char isgmt;        /* Transition times are in GMT.  */
};

struct leap
{
    time_t transition;          /* Time the transition takes effect.  */
    long int change;            /* Seconds of correction to apply.  */
};

//static struct ttinfo *find_transition (time_t timer) internal_function;
//static void compute_tzname_max (size_t) internal_function;

static size_t num_transitions;
static time_t *transitions;
static unsigned char *type_idxs;
static size_t num_types;
static struct ttinfo *types;
static char *zone_names;
static long int rule_stdoff;
static long int rule_dstoff;
static size_t num_leaps;
static struct leap *leaps;
static char *tzspec;

static inline int bswap_32(const int i) {
    printf("Got %.2x %.2x %.2x %.2x\n", ((unsigned char *)&i)[0], ((unsigned char *)&i)[1], ((unsigned char *)&i)[2], ((unsigned char *)&i)[3]);

    int top = (i & (0xff << 0)) << 24;
    int second = (i & (0xff << 8)) << 16;
    int third = (i & (0xff << 16)) << 8;
    int bottom = (i & (0xff << 24)) << 0;

    printf("Top: %x\nSecond: %x\nThird: %x\nBottom: %x\n",top,second,third,bottom);
    printf("Mixed: %x\n",top|second|third|bottom);
    return ((i & 0xff) << 24) |
           ((i & (0xff << 8)) << 16) |
           ((i & (0xff << 16)) << 8) |
           ((i & (0xff << 24)));

}

static size_t __tzname_cur_max = 0;

static void
compute_tzname_max (size_t chars)
{
  const char *p;

  p = zone_names;
  do
    {
      const char *start = p;
      while (*p != '\0')
	++p;
      if ((size_t) (p - start) > __tzname_cur_max)
	__tzname_cur_max = p - start;
    }
  while (++p < &zone_names[chars]);
}

static inline int
decode (const void *ptr)
{
    /*
    if ((BYTE_ORDER == BIG_ENDIAN) && sizeof (int) == 4)//
        return *(const int *) ptr;
    else if (BYTE_ORDER == LITTLE_ENDIAN && sizeof (int) == 4)
        return bswap_32 (*(const int *) ptr);
    else
    */
    {
        const unsigned char *p = ptr;
        int result = *p & (1 << (CHAR_BIT - 1)) ? ~0 : 0;

        result = (result << 8) | *p++;
        result = (result << 8) | *p++;
        result = (result << 8) | *p++;
        result = (result << 8) | *p++;

        return result;
    }
}


void
__tzfile_read (const char *file, size_t extra, char **extrap)
{
  static const char default_tzdir[] = TZDIR;
  size_t num_isstd, num_isgmt;
  register FILE *f;
  struct tzhead tzhead;
  size_t chars;
  register size_t i;
  size_t total_size;
  size_t types_idx;
  size_t leaps_idx;
  int was_using_tzfile = __use_tzfile;
  int trans_width = 4;
  size_t tzspec_len;

  if (sizeof (time_t) != 4 && sizeof (time_t) != 8)
    abort ();

  __use_tzfile = 0;

  if (file == NULL)
    /* No user specification; use the site-wide default.  */
    file = TZDEFAULT;
  else if (*file == '\0')
    /* User specified the empty string; use UTC with no leap seconds.  */
    goto ret_free_transitions;
  else
    {
      /* We must not allow to read an arbitrary file in a setuid
	 program.  So we fail for any file which is not in the
	 directory hierachy starting at TZDIR
	 and which is not the system wide default TZDEFAULT.  */
      if (1
	  && ((*file == '/'
	       && memcmp (file, TZDEFAULT, sizeof TZDEFAULT)
	       && memcmp (file, default_tzdir, sizeof (default_tzdir) - 1))
	      || strstr (file, "../") != NULL))
	/* This test is certainly a bit too restrictive but it should
	   catch all critical cases.  */
	goto ret_free_transitions;
    }

  if (*file != '/')
    {
      const char *tzdir;
      unsigned int len, tzdir_len;
      char *new, *tmp;

      tzdir = getenv ("TZDIR");
      if (tzdir == NULL || *tzdir == '\0')
	{
	  tzdir = default_tzdir;
	  tzdir_len = sizeof (default_tzdir) - 1;
	}
      else
	tzdir_len = strlen (tzdir);
      len = strlen (file) + 1;
      new = (char *) malloc(tzdir_len + 1 + len);
      tmp = (char *)__mempcpy (new, tzdir, tzdir_len);
      *tmp++ = '/';
      memcpy (tmp, file, len);
      file = new;
    }

  /* If we were already using tzfile, check whether the file changed.  */
  struct stat st;
  if (was_using_tzfile
      && stat (file, &st) == 0
      && tzfile_ino == st.st_ino && tzfile_dev == st.st_dev
      && tzfile_mtime == st.st_mtime)
    {
      /* Nothing to do.  */
      __use_tzfile = 1;
      return;
    }

  /* Note the file is opened with cancellation in the I/O functions
     disabled.  */
  f = fopen (file, "rc");
  if (f == NULL)
    goto ret_free_transitions;

  /* Get information about the file we are actually using.  */
  if (fstat64 (fileno (f), &st) != 0)
    {
      fclose (f);
      goto ret_free_transitions;
    }

  free ((void *) transitions);
  transitions = NULL;

  /* Remember the inode and device number and modification time.  */
  tzfile_dev = st.st_dev;
  tzfile_ino = st.st_ino;
  tzfile_mtime = st.st_mtime;

  /* No threads reading this stream.  */
  //__fsetlocking (f, FSETLOCKING_BYCALLER);

 read_again:
  if (__builtin_expect (fread_unlocked ((void *) &tzhead, sizeof (tzhead),
					1, f) != 1, 0)
      || memcmp (tzhead.tzh_magic, TZ_MAGIC, sizeof (tzhead.tzh_magic)) != 0)
    goto lose;

  num_transitions = (size_t) decode (tzhead.tzh_timecnt);
  num_types = (size_t) decode (tzhead.tzh_typecnt);
  chars = (size_t) decode (tzhead.tzh_charcnt);
  num_leaps = (size_t) decode (tzhead.tzh_leapcnt);
  num_isstd = (size_t) decode (tzhead.tzh_ttisstdcnt);
  num_isgmt = (size_t) decode (tzhead.tzh_ttisgmtcnt);

  /* For platforms with 64-bit time_t we use the new format if available.  */
  if (sizeof (time_t) == 8 && trans_width == 4
      && tzhead.tzh_version[0] != '\0')
    {
      /* We use the 8-byte format.  */
      trans_width = 8;

      /* Position the stream before the second header.  */
      size_t to_skip = (num_transitions * (4 + 1)
			+ num_types * 6
			+ chars
			+ num_leaps * 8
			+ num_isstd
			+ num_isgmt);
      if (fseek (f, to_skip, SEEK_CUR) != 0)
	goto lose;

      goto read_again;
    }

  total_size = num_transitions * (sizeof (time_t) + 1);
  total_size = ((total_size + __alignof__ (struct ttinfo) - 1)
		& ~(__alignof__ (struct ttinfo) - 1));
  types_idx = total_size;
  total_size += num_types * sizeof (struct ttinfo) + chars;
  total_size = ((total_size + __alignof__ (struct leap) - 1)
		& ~(__alignof__ (struct leap) - 1));
  leaps_idx = total_size;
  total_size += num_leaps * sizeof (struct leap);
  tzspec_len = (sizeof (time_t) == 8 && trans_width == 8
		? st.st_size - (ftello (f)
				+ num_transitions * (8 + 1)
				+ num_types * 6
				+ chars
				+ num_leaps * 8
				+ num_isstd
				+ num_isgmt) - 1 : 0);

  /* Allocate enough memory including the extra block requested by the
     caller.  */
  transitions = (time_t *) malloc (total_size + tzspec_len + extra);
  if (transitions == NULL)
    goto lose;

  type_idxs = (unsigned char *) transitions + (num_transitions
					       * sizeof (time_t));
  types = (struct ttinfo *) ((char *) transitions + types_idx);
  zone_names = (char *) types + num_types * sizeof (struct ttinfo);
  leaps = (struct leap *) ((char *) transitions + leaps_idx);
  if (sizeof (time_t) == 8 && trans_width == 8)
    tzspec = (char *) leaps + num_leaps * sizeof (struct leap) + extra;
  else
    tzspec = NULL;
  if (extra > 0)
    *extrap = (char *) &leaps[num_leaps];

  if (sizeof (time_t) == 4 || __builtin_expect (trans_width == 8, 1))
    {
      if (__builtin_expect (fread_unlocked (transitions, trans_width + 1,
					    num_transitions, f)
			    != num_transitions, 0))
	goto lose;
    }
  else
    {
      if (__builtin_expect (fread_unlocked (transitions, 4, num_transitions, f)
			    != num_transitions, 0)
	  || __builtin_expect (fread_unlocked (type_idxs, 1, num_transitions,
					       f) != num_transitions, 0))
	goto lose;
    }

  /* Check for bogus indices in the data file, so we can hereafter
     safely use type_idxs[T] as indices into `types' and never crash.  */
  for (i = 0; i < num_transitions; ++i)
    if (__builtin_expect (type_idxs[i] >= num_types, 0))
      goto lose;

  if ((BYTE_ORDER != BIG_ENDIAN && (sizeof (time_t) == 4 || trans_width == 4))
      || (BYTE_ORDER == BIG_ENDIAN && sizeof (time_t) == 8
	  && trans_width == 4))
    {
      /* Decode the transition times, stored as 4-byte integers in
	 network (big-endian) byte order.  We work from the end of
	 the array so as not to clobber the next element to be
	 processed when sizeof (time_t) > 4.  */
      i = num_transitions;
      while (i-- > 0)
	transitions[i] = decode ((char *) transitions + i * 4);
    }
  else if (BYTE_ORDER != BIG_ENDIAN && sizeof (time_t) == 8)
    {
      /* Decode the transition times, stored as 8-byte integers in
	 network (big-endian) byte order.  */
      for (i = 0; i < num_transitions; ++i)
	transitions[i] = decode64 ((char *) transitions + i * 8);
    }

  for (i = 0; i < num_types; ++i)
    {
      unsigned char x[4];
      int c;
      if (__builtin_expect (fread_unlocked (x, 1, sizeof (x), f) != sizeof (x),
			    0))
	goto lose;
      c = getc_unlocked (f);
      if (__builtin_expect ((unsigned int) c > 1u, 0))
	goto lose;
      types[i].isdst = c;
      c = getc_unlocked (f);
      if (__builtin_expect ((size_t) c > chars, 0))
	/* Bogus index in data file.  */
	goto lose;
      types[i].idx = c;
      types[i].offset = (long int) decode (x);
    }

  if (__builtin_expect (fread_unlocked (zone_names, 1, chars, f) != chars, 0))
    goto lose;

  for (i = 0; i < num_leaps; ++i)
    {
      unsigned char x[8];
      if (__builtin_expect (fread_unlocked (x, 1, trans_width, f)
			    != trans_width, 0))
	goto lose;
      if (sizeof (time_t) == 4 || trans_width == 4)
	leaps[i].transition = (time_t) decode (x);
      else
	leaps[i].transition = (time_t) decode64 (x);

      if (__builtin_expect (fread_unlocked (x, 1, 4, f) != 4, 0))
	goto lose;
      leaps[i].change = (long int) decode (x);
    }

  for (i = 0; i < num_isstd; ++i)
    {
      int c = getc_unlocked (f);
      if (__builtin_expect (c == EOF, 0))
	goto lose;
      types[i].isstd = c != 0;
    }
  while (i < num_types)
    types[i++].isstd = 0;

  for (i = 0; i < num_isgmt; ++i)
    {
      int c = getc_unlocked (f);
      if (__builtin_expect (c == EOF, 0))
	goto lose;
      types[i].isgmt = c != 0;
    }
  while (i < num_types)
    types[i++].isgmt = 0;

  /* Read the POSIX TZ-style information if possible.  */
  if (sizeof (time_t) == 8 && tzspec != NULL)
    {
      /* Skip over the newline first.  */
      if (getc_unlocked (f) != '\n'
	  || (fread_unlocked (tzspec, 1, tzspec_len - 1, f)
	      != tzspec_len - 1))
	tzspec = NULL;
      else
	tzspec[tzspec_len - 1] = '\0';
    }
  else if (sizeof (time_t) == 4 && tzhead.tzh_version[0] != '\0')
    {
      /* Get the TZ string.  */
      if (__builtin_expect (fread_unlocked ((void *) &tzhead, sizeof (tzhead),
					    1, f) != 1, 0)
	  || (memcmp (tzhead.tzh_magic, TZ_MAGIC, sizeof (tzhead.tzh_magic))
	      != 0))
	goto lose;

      size_t num_transitions2 = (size_t) decode (tzhead.tzh_timecnt);
      size_t num_types2 = (size_t) decode (tzhead.tzh_typecnt);
      size_t chars2 = (size_t) decode (tzhead.tzh_charcnt);
      size_t num_leaps2 = (size_t) decode (tzhead.tzh_leapcnt);
      size_t num_isstd2 = (size_t) decode (tzhead.tzh_ttisstdcnt);
      size_t num_isgmt2 = (size_t) decode (tzhead.tzh_ttisgmtcnt);

      /* Position the stream before the second header.  */
      size_t to_skip = (num_transitions2 * (8 + 1)
			+ num_types2 * 6
			+ chars2
			+ num_leaps2 * 12
			+ num_isstd2
			+ num_isgmt2);
      off_t off;
      if (fseek (f, to_skip, SEEK_CUR) != 0
	  || (off = ftello (f)) < 0
	  || st.st_size < off + 2)
	goto lose;

      tzspec_len = st.st_size - off - 1;
      char *tzstr = alloca (tzspec_len);
      if (getc_unlocked (f) != '\n'
	  || (fread_unlocked (tzstr, 1, tzspec_len - 1, f) != tzspec_len - 1))
	goto lose;
      tzstr[tzspec_len - 1] = '\0';
      tzspec = __tzstring (tzstr);
    }

  fclose (f);

  /* First "register" all timezone names.  */
  for (i = 0; i < num_types; ++i)
    (void) __tzstring (&zone_names[types[i].idx]);

  /* Find the standard and daylight time offsets used by the rule file.
     We choose the offsets in the types of each flavor that are
     transitioned to earliest in time.  */
  __tzname[0] = NULL;
  __tzname[1] = NULL;
  for (i = num_transitions; i > 0; )
    {
      int type = type_idxs[--i];
      int dst = types[type].isdst;

      if (__tzname[dst] == NULL)
	{
	  int idx = types[type].idx;

	  __tzname[dst] = __tzstring (&zone_names[idx]);

	  if (__tzname[1 - dst] != NULL)
	    break;
	}
    }
  if (__tzname[0] == NULL)
    {
      /* This should only happen if there are no transition rules.
	 In this case there should be only one single type.  */
      assert (num_types == 1);
      __tzname[0] = __tzstring (zone_names);
    }
  if (__tzname[1] == NULL)
    __tzname[1] = __tzname[0];

  compute_tzname_max (chars);

  if (num_transitions == 0)
    /* Use the first rule (which should also be the only one).  */
    rule_stdoff = rule_dstoff = types[0].offset;
  else
    {
      int stdoff_set = 0, dstoff_set = 0;
      rule_stdoff = rule_dstoff = 0;
      i = num_transitions - 1;
      do
	{
	  if (!stdoff_set && !types[type_idxs[i]].isdst)
	    {
	      stdoff_set = 1;
	      rule_stdoff = types[type_idxs[i]].offset;
	    }
	  else if (!dstoff_set && types[type_idxs[i]].isdst)
	    {
	      dstoff_set = 1;
	      rule_dstoff = types[type_idxs[i]].offset;
	    }
	  if (stdoff_set && dstoff_set)
	    break;
	}
      while (i-- > 0);

      if (!dstoff_set)
	rule_dstoff = rule_stdoff;
    }

  __daylight = rule_stdoff != rule_dstoff;
  __timezone = -rule_stdoff;

  __use_tzfile = 1;
  return;

 lose:
  fclose (f);
 ret_free_transitions:
  free ((void *) transitions);
  transitions = NULL;
}
void
__tzfile_compute (time_t timer, int use_localtime,
		  long int *leap_correct, int *leap_hit,
		  struct tm *tp)
{
  register size_t i;

  if (use_localtime)
    {
      __tzname[0] = NULL;
      __tzname[1] = NULL;

      if (__builtin_expect (num_transitions == 0 || timer < transitions[0], 0))
	{
	  /* TIMER is before any transition (or there are no transitions).
	     Choose the first non-DST type
	     (or the first if they're all DST types).  */
	  i = 0;
	  while (i < num_types && types[i].isdst)
	    {
	      if (__tzname[1] == NULL)
		__tzname[1] = __tzstring (&zone_names[types[i].idx]);

	      ++i;
	    }

	  if (i == num_types)
	    i = 0;
	  __tzname[0] = __tzstring (&zone_names[types[i].idx]);
	  if (__tzname[1] == NULL)
	    {
	      size_t j = i;
	      while (j < num_types)
		if (types[j].isdst)
		  {
		    __tzname[1] = __tzstring (&zone_names[types[j].idx]);
		    break;
		  }
		else
		  ++j;
	    }
	}
      else if (__builtin_expect (timer >= transitions[num_transitions - 1], 0))
	{
	  if (__builtin_expect (tzspec == NULL, 0))
	    {
	    use_last:
	      i = num_transitions;
	      goto found;
	    }

	  /* Parse the POSIX TZ-style string.  */
	  __tzset_parse_tz (tzspec);

	  /* Convert to broken down structure.  If this fails do not
	     use the string.  */
	  if (__builtin_expect (! __offtime (&timer, 0, tp), 0))
	    goto use_last;

	  /* Use the rules from the TZ string to compute the change.  */
	  __tz_compute (timer, tp, 1);

	  /* If tzspec comes from posixrules loaded by __tzfile_default,
	     override the STD and DST zone names with the ones user
	     requested in TZ envvar.  */
	  if (__builtin_expect (zone_names == (char *) &leaps[num_leaps], 0))
	    {
	      assert (num_types == 2);
	      __tzname[0] = __tzstring (zone_names);
	      __tzname[1] = __tzstring (&zone_names[strlen (zone_names) + 1]);
	    }

	  *leap_correct = 0L;
	  *leap_hit = 0;
	  return;
	}
      else
	{
	  /* Find the first transition after TIMER, and
	     then pick the type of the transition before it.  */
	  size_t lo = 0;
	  size_t hi = num_transitions - 1;
	  /* Assume that DST is changing twice a year and guess initial
	     search spot from it.
	     Half of a gregorian year has on average 365.2425 * 86400 / 2
	     = 15778476 seconds.  */
	  i = (transitions[num_transitions - 1] - timer) / 15778476;
	  if (i < num_transitions)
	    {
	      i = num_transitions - 1 - i;
	      if (timer < transitions[i])
		{
		  if (i < 10 || timer >= transitions[i - 10])
		    {
		      /* Linear search.  */
		      while (timer < transitions[i - 1])
			--i;
		      goto found;
		    }
		  hi = i - 10;
		}
	      else
		{
		  if (i + 10 >= num_transitions || timer < transitions[i + 10])
		    {
		      /* Linear search.  */
		      while (timer >= transitions[i])
			++i;
		      goto found;
		    }
		  lo = i + 10;
		}
	    }

	  /* Binary search.  */
	  /* assert (timer >= transitions[lo] && timer < transitions[hi]); */
	  while (lo + 1 < hi)
	    {
	      i = (lo + hi) / 2;
	      if (timer < transitions[i])
		hi = i;
	      else
		lo = i;
	    }
	  i = hi;

	found:
	  /* assert (timer >= transitions[i - 1]
	     && (i == num_transitions || timer < transitions[i])); */
	  __tzname[types[type_idxs[i - 1]].isdst]
	    = __tzstring (&zone_names[types[type_idxs[i - 1]].idx]);
	  size_t j = i;
	  while (j < num_transitions)
	    {
	      int type = type_idxs[j];
	      int dst = types[type].isdst;
	      int idx = types[type].idx;

	      if (__tzname[dst] == NULL)
		{
		  __tzname[dst] = __tzstring (&zone_names[idx]);

		  if (__tzname[1 - dst] != NULL)
		    break;
		}

	      ++j;
	    }

	  if (__builtin_expect (__tzname[0] == NULL, 0))
	    __tzname[0] = __tzname[1];

	  i = type_idxs[i - 1];
	}

      struct ttinfo *info = &types[i];
      __daylight = rule_stdoff != rule_dstoff;
      __timezone = -rule_stdoff;

      if (__tzname[0] == NULL)
	{
	  /* This should only happen if there are no transition rules.
	     In this case there should be only one single type.  */
	  assert (num_types == 1);
	  __tzname[0] = __tzstring (zone_names);
	}
      if (__tzname[1] == NULL)
	/* There is no daylight saving time.  */
	__tzname[1] = __tzname[0];
      tp->tm_isdst = info->isdst;
      assert (strcmp (&zone_names[info->idx], __tzname[tp->tm_isdst]) == 0);
      tp->tm_zone = __tzname[tp->tm_isdst];
      tp->tm_gmtoff = info->offset;
    }

  *leap_correct = 0L;
  *leap_hit = 0;

  /* Find the last leap second correction transition time before TIMER.  */
  i = num_leaps;
  do
    if (i-- == 0)
      return;
  while (timer < leaps[i].transition);

  /* Apply its correction.  */
  *leap_correct = leaps[i].change;

  if (timer == leaps[i].transition && /* Exactly at the transition time.  */
      ((i == 0 && leaps[i].change > 0) ||
       leaps[i].change > leaps[i - 1].change))
    {
      *leap_hit = 1;
      while (i > 0
	     && leaps[i].transition == leaps[i - 1].transition + 1
	     && leaps[i].change == leaps[i - 1].change + 1)
	{
	  ++*leap_hit;
	  --i;
	}
    }
}
int main(int argc, char * argv[]) {
    if (argc < 2) {
        __tzfile_read(NULL, 0, NULL);
    }
    else {
        __tzfile_read(argv[1], 0, NULL);
    }
    return 0;
}
