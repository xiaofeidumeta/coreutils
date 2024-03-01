/* mu -- summarize memory usage
   Copyright (C) 1988-2024 Free Software Foundation, Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>
#include <getopt.h>
#include <sys/types.h>
#include "system.h"
#include "argmatch.h"
#include "argv-iter.h"
#include "assure.h"
#include "di-set.h"
#include "exclude.h"
#include "fprintftime.h"
#include "human.h"
#include "mountlist.h"
#include "quote.h"
#include "stat-size.h"
#include "stat-time.h"
#include "stdio--.h"
#include "xfts.h"
#include "xstrtol.h"
#include "xstrtol-error.h"

#include <fcntl.h>
#include <linux/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

extern bool fts_debug;

/* The official name of this program (e.g., no 'g' prefix).  */
#define PROGRAM_NAME "mu"

#define AUTHORS \
  proper_name ("Xiaofei Du")

#if DU_DEBUG
# define FTS_CROSS_CHECK(Fts) fts_cross_check (Fts)
#else
# define FTS_CROSS_CHECK(Fts)
#endif

/* A set of dev/ino pairs to help identify files and directories
   whose sizes have already been counted.  */
static struct di_set *di_files;

/* A set containing a dev/ino pair for each local mount point directory.  */
static struct di_set *di_mnt;

/* Keep track of the preceding "level" (depth in hierarchy)
   from one call of process_file to the next.  */
static size_t prev_level;

struct muinfo
{
  uintmax_t cache_size;
  uintmax_t dirty_size;
  uintmax_t writeback_size;
  uintmax_t evicted_size;
  uintmax_t recently_evicted_size;
  struct timespec tmax;
};

static inline void
muinfo_init(struct muinfo* mui)
{
  mui->cache_size = 0;
  mui->dirty_size = 0;
  mui->writeback_size = 0;
  mui->evicted_size = 0;
  mui->recently_evicted_size = 0;
  mui->tmax.tv_sec = TYPE_MINIMUM (time_t);
  mui->tmax.tv_nsec = -1;
}

static inline void
muinfo_add(struct muinfo* first, const struct muinfo* second)
{
  uintmax_t sum = first->cache_size + second->cache_size;
  first->cache_size = first->cache_size <= sum ? sum : UINTMAX_MAX;

  sum = first->dirty_size + second->dirty_size;
  first->dirty_size = first->dirty_size <= sum ? sum : UINTMAX_MAX;

  sum = first->writeback_size + second->writeback_size;
  first->writeback_size = first->writeback_size <= sum ? sum : UINTMAX_MAX;

  sum = first->evicted_size + second->evicted_size;
  first->evicted_size = first->evicted_size <= sum ? sum : UINTMAX_MAX;

  sum = first->recently_evicted_size + second->recently_evicted_size;
  first->recently_evicted_size = first->recently_evicted_size <= sum ? sum : UINTMAX_MAX;

  if (timespec_cmp (first->tmax, second->tmax) < 0)
    first->tmax = second->tmax;
}

struct mulevel
{
  struct muinfo ent;
  struct muinfo subdir;
};

/* If true, display counts for all files, not just directories.  */
static bool opt_all = false;

/* If true, count each hard link of files with multiple links.  */
static bool opt_count_all = false;

/* If true, hash all files to look for hard links.  */
static bool hash_all;

/* If true, output the NUL byte instead of a newline at the end of each line. */
static bool opt_nul_terminate_output = false;

/* If true, print a grand total at the end.  */
static bool print_grand_total = false;

/* If nonzero, do not add sizes of subdirectories.  */
static bool opt_separate_dirs = false;

/* Show the total for each directory (and file if --all) that is at
   most MAX_DEPTH levels down from the root of the hierarchy.  The root
   is at level 0, so 'mu --max-depth=0' is equivalent to 'mu -s'.  */
static idx_t max_depth = IDX_MAX;

/* Only output entries with at least this SIZE if positive,
   or at most if negative.  See --threshold option.  */
static intmax_t opt_threshold = 0;

/* Human-readable options for output.  */
static int human_output_opts;

/* If true, print most recently modified date, using the specified format.  */
static bool opt_time = false;

/* Type of time to display. controlled by --time.  */

enum time_type
  {
    time_mtime,			/* default */
    time_ctime,
    time_atime
  };

static enum time_type time_type = time_mtime;

/* User specified date / time style */
static char const *time_style = nullptr;

/* Format used to display date / time. Controlled by --time-style */
static char const *time_format = nullptr;

/* The local time zone rules, as per the TZ environment variable.  */
static timezone_t localtz;

/* The units to use when printing sizes.  */
static uintmax_t output_block_size;

/* File name patterns to exclude.  */
static struct exclude *exclude;

static struct muinfo tot_mui;

#define IS_DIR_TYPE(Type)	\
  ((Type) == FTS_DP		\
   || (Type) == FTS_DNR)

/* For long options that have no equivalent short option, use a
   non-character as a pseudo short option, starting with CHAR_MAX + 1.  */
enum
{
  EXCLUDE_OPTION = CHAR_MAX + 1,
  FILES0_FROM_OPTION,
  HUMAN_SI_OPTION,
  FTS_DEBUG,
  TIME_OPTION,
  TIME_STYLE_OPTION,
};

static struct option const long_options[] =
{
  {"all", no_argument, nullptr, 'a'},
  {"block-size", required_argument, nullptr, 'B'},
  {"bytes", no_argument, nullptr, 'b'},
  {"count-links", no_argument, nullptr, 'l'},
  /* {"-debug", no_argument, nullptr, FTS_DEBUG}, */
  {"dereference", no_argument, nullptr, 'L'},
  {"dereference-args", no_argument, nullptr, 'D'},
  {"exclude", required_argument, nullptr, EXCLUDE_OPTION},
  {"exclude-from", required_argument, nullptr, 'X'},
  {"files0-from", required_argument, nullptr, FILES0_FROM_OPTION},
  {"human-readable", no_argument, nullptr, 'h'},
  {"si", no_argument, nullptr, HUMAN_SI_OPTION},
  {"max-depth", required_argument, nullptr, 'd'},
  {"null", no_argument, nullptr, '0'},
  {"no-dereference", no_argument, nullptr, 'P'},
  {"one-file-system", no_argument, nullptr, 'x'},
  {"separate-dirs", no_argument, nullptr, 'S'},
  {"summarize", no_argument, nullptr, 's'},
  {"total", no_argument, nullptr, 'c'},
  {"threshold", required_argument, nullptr, 't'},
  {"time", optional_argument, nullptr, TIME_OPTION},
  {"time-style", required_argument, nullptr, TIME_STYLE_OPTION},
  {"format", required_argument, nullptr, 'f'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
  {nullptr, 0, nullptr, 0}
};

static char const *const time_args[] =
{
  "atime", "access", "use", "ctime", "status", nullptr
};
static enum time_type const time_types[] =
{
  time_atime, time_atime, time_atime, time_ctime, time_ctime
};
ARGMATCH_VERIFY (time_args, time_types);

/* 'full-iso' uses full ISO-style dates and times.  'long-iso' uses longer
   ISO-style timestamps, though shorter than 'full-iso'.  'iso' uses shorter
   ISO-style timestamps.  */
enum time_style
  {
    full_iso_time_style,       /* --time-style=full-iso */
    long_iso_time_style,       /* --time-style=long-iso */
    iso_time_style	       /* --time-style=iso */
  };

static char const *const time_style_args[] =
{
  "full-iso", "long-iso", "iso", nullptr
};
static enum time_style const time_style_types[] =
{
  full_iso_time_style, long_iso_time_style, iso_time_style
};
ARGMATCH_VERIFY (time_style_args, time_style_types);

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    emit_try_help ();
  else
    {
      printf (_("\
Usage: %s [OPTION]... [FILE]...\n\
  or:  %s [OPTION]... --files0-from=F\n\
"), program_name, program_name);
      fputs (_("\
Summarize memory usage of the set of FILEs, recursively for directories.\n\
"), stdout);

      emit_mandatory_arg_note ();

      fputs (_("\
  -0, --null            end each output line with NUL, not newline\n\
  -a, --all             write counts for all files, not just directories\n\
"), stdout);
      fputs (_("\
  -B, --block-size=SIZE  scale sizes by SIZE before printing them; e.g.,\n\
                           '-BM' prints sizes in units of 1,048,576 bytes;\n\
                           see SIZE format below\n\
  -b, --bytes           equivalent to '--block-size=1'\n\
  -c, --total           produce a grand total\n\
  -D, --dereference-args  dereference only symlinks that are listed on the\n\
                          command line\n\
  -d, --max-depth=N     print the total for a directory (or file, with --all)\n\
                          only if it is N or fewer levels below the command\n\
                          line argument;  --max-depth=0 is the same as\n\
                          --summarize\n\
"), stdout);
      fputs (_("\
      --files0-from=F   summarize device usage of the\n\
                          NUL-terminated file names specified in file F;\n\
                          if F is -, then read names from standard input\n\
  -f, --format=FORMAT   use the specified FORMAT for output instead of the\n\
                          default; Only cached bytes are printed by default\n\
  -H                    equivalent to --dereference-args (-D)\n\
  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\
\n\
"), stdout);
      fputs (_("\
  -k                    like --block-size=1K\n\
  -L, --dereference     dereference all symbolic links\n\
  -l, --count-links     count sizes many times if hard linked\n\
  -m                    like --block-size=1M\n\
"), stdout);
      fputs (_("\
  -P, --no-dereference  don't follow any symbolic links (this is the default)\n\
  -S, --separate-dirs   for directories do not include size of subdirectories\n\
      --si              like -h, but use powers of 1000 not 1024\n\
  -s, --summarize       display only a total for each argument\n\
"), stdout);
      fputs (_("\
  -t, --threshold=SIZE  exclude entries smaller than SIZE if positive,\n\
                          or entries greater than SIZE if negative\n\
      --time            show time of the last modification of any file in the\n\
                          directory, or any of its subdirectories\n\
      --time=WORD       show time as WORD instead of modification time:\n\
                          atime, access, use, ctime or status\n\
      --time-style=STYLE  show times using STYLE, which can be:\n\
                            full-iso, long-iso, iso, or +FORMAT;\n\
                            FORMAT is interpreted like in 'date'\n\
"), stdout);
      fputs (_("\
  -X, --exclude-from=FILE  exclude files that match any pattern in FILE\n\
      --exclude=PATTERN    exclude files that match PATTERN\n\
  -x, --one-file-system    skip directories on different file systems\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\n\
The valid format sequences are:\n\
\n\
  %c   memory cached in the page cache\n\
  %d   dirty memory (have been modified and not yet written back\n\
         to persistent storage)\n\
  %w   memory currently being written back\n\
  %e   memory were once resident in the cache but has since been forced out\n\
  %r   memory that has been forced out in the recent past. In this case, the\n\
         'recent past' is defined by the memory that has been evicted since\n\
         the memory in question was forced out\n\
"), stdout);
      emit_blocksize_note ("MU");
      emit_size_note ();
      emit_ancillary_info (PROGRAM_NAME);
    }
  exit (status);
}

/* Try to insert the INO/DEV pair into DI_SET.
   Return true if the pair is successfully inserted,
   false if the pair was already there.  */
static bool
hash_ins (struct di_set *di_set, ino_t ino, dev_t dev)
{
  int inserted = di_set_insert (di_set, dev, ino);
  if (inserted < 0)
    xalloc_die ();
  return inserted;
}

/* FIXME: this code is nearly identical to code in date.c  */
/* Display the date and time in WHEN according to the format specified
   in FORMAT.  */

static void
show_date (char const *format, struct timespec when, timezone_t tz)
{
  struct tm tm;
  if (localtime_rz (tz, &when.tv_sec, &tm))
    fprintftime (stdout, format, &tm, tz, when.tv_nsec);
  else
    {
      char buf[INT_BUFSIZE_BOUND (intmax_t)];
      char *when_str = timetostr (when.tv_sec, buf);
      error (0, 0, _("time %s is out of range"), quote (when_str));
      fputs (when_str, stdout);
    }
}

/* Print N_BYTES.  Convert it to a readable value before printing.  */

static void
print_only_size (uintmax_t n_bytes)
{
  char buf[LONGEST_HUMAN_READABLE + 1];
  fputs ((n_bytes == UINTMAX_MAX
          ? _("Infinity")
          : human_readable (n_bytes, buf, human_output_opts,
                            1, output_block_size)),
         stdout);
}

static void
mu_print_stat (const struct muinfo *pmui, char m)
{
  switch (m)
    {
      case 'c':
        print_only_size (pmui->cache_size);
        break;
      case 'd':
        print_only_size (pmui->dirty_size);
        break;
      case 'w':
        print_only_size (pmui->writeback_size);
        break;
      case 'e':
        print_only_size (pmui->evicted_size);
        break;
      case 'r':
        print_only_size (pmui->recently_evicted_size);
        break;
      default:
        putchar('?');
        break;
    }
}

static void
mu_print_size (const struct muinfo *pmui, char const *string, char const *format)
{
  if (format)
    {
      for (char const *b=format; *b; ++b)
        {
           if (*b == '%')
             {
               b += 1;
               char fmt_char = *b;
               switch (fmt_char)
                 {
                    case '\0':
                      --b;
                      FALLTHROUGH;
                    case '%':
                      putchar('%');
                      break;
                    default:
                      mu_print_stat (pmui, *b);
                      break;
                 }
             }
           else
             {
               putchar(*b);
             }
        }
    }
  else
    {
      /* Only print cache size by default if no format is provided */
      print_only_size (pmui->cache_size);
    }

  if (opt_time)
    {
      putchar ('\t');
      show_date (time_format, pmui->tmax, localtz);
    }
  printf ("\t%s%c", string, opt_nul_terminate_output ? '\0' : '\n');
  fflush (stdout);
}

/* Fill the di_mnt set with local mount point dev/ino pairs.  */

static void
fill_mount_table (void)
{
  struct mount_entry *mnt_ent = read_file_system_list (false);
  while (mnt_ent)
    {
      struct mount_entry *mnt_free;
      if (!mnt_ent->me_remote && !mnt_ent->me_dummy)
        {
          struct stat buf;
          if (!stat (mnt_ent->me_mountdir, &buf))
            hash_ins (di_mnt, buf.st_ino, buf.st_dev);
          else
            {
              /* Ignore stat failure.  False positives are too common.
                 E.g., "Permission denied" on /run/user/<name>/gvfs.  */
            }
        }

      mnt_free = mnt_ent;
      mnt_ent = mnt_ent->me_next;
      free_mount_entry (mnt_free);
    }
}

/* This function checks whether any of the directories in the cycle that
   fts detected is a mount point.  */

static bool
mount_point_in_fts_cycle (FTSENT const *ent)
{
  FTSENT const *cycle_ent = ent->fts_cycle;

  if (!di_mnt)
    {
      /* Initialize the set of dev,inode pairs.  */
      di_mnt = di_set_alloc ();
      if (!di_mnt)
        xalloc_die ();

      fill_mount_table ();
    }

  while (ent && ent != cycle_ent)
    {
      if (di_set_lookup (di_mnt, ent->fts_statp->st_dev,
                         ent->fts_statp->st_ino) > 0)
        {
          return true;
        }
      ent = ent->fts_parent;
    }

  return false;
}

static bool
get_file_cachestat(const FTSENT* ent, const struct stat* sb, enum time_type tt, struct muinfo* mui)
{
  bool ret;
  const char* filename = ent->fts_path;
  int fd = -1;

  muinfo_init(mui);

  /* skip calling cachestat for symlinks */
  if (ent->fts_info == FTS_SL) {
    goto out_time;
  }

  fd = open(filename, O_RDONLY, 0400);
  if (fd == -1) {
    /* UNIX domain socket file */
    if (errno == ENXIO) {
      goto out_time;
    }

    /* file does not exist */
    if (access(filename, F_OK)) {
      goto out_time;
    }

    return false;
  }

  struct cachestat cs;
  struct cachestat_range cs_range = {0, sb->st_size};
  if (syscall(__NR_cachestat, fd, &cs_range, &cs, 0)) {
    ret = false;
    goto out;
  }

  long pagesize = sysconf(_SC_PAGESIZE);
  mui->cache_size = cs.nr_cache * pagesize;
  mui->dirty_size = cs.nr_dirty * pagesize;
  mui->writeback_size = cs.nr_writeback * pagesize;
  mui->evicted_size = cs.nr_evicted * pagesize;
  mui->recently_evicted_size = cs.nr_recently_evicted * pagesize;

out_time:
  mui->tmax = (tt == time_mtime ? get_stat_mtime(sb)
               : tt == time_atime ? get_stat_atime(sb)
               : get_stat_ctime(sb));

  ret = true;

out:
  if (fd != -1) {
    close(fd);
  }

  return ret;
}

/* This function is called once for every file system object that fts
   encounters.  fts does a depth-first traversal.  This function knows
   that and accumulates per-directory totals based on changes in
   the depth of the current entry.  It returns true on success.  */

static bool
process_file (FTS *fts, FTSENT *ent, char const *format)
{
  bool ok = true;

  struct muinfo mui;
  struct muinfo mui_to_print;

  size_t level;
  static size_t n_alloc;
  /* First element of the structure contains:
     The sum of the sizes of all entries in the single directory
     at the corresponding level.  Although this does include the sizes
     corresponding to each subdirectory, it does not include the size of
     any file in a subdirectory. Also corresponding last modified date.
     Second element of the structure contains:
     The sum of the sizes of all entries in the hierarchy at or below the
     directory at the specified level.  */

  static struct mulevel *mulvl;

  char const *file = ent->fts_path;
  const struct stat *sb = ent->fts_statp;
  int info = ent->fts_info;

  if (info == FTS_DNR)
    {
      /* An error occurred, but the size is known, so count it.  */
      error (0, ent->fts_errno, _("cannot read directory %s"), quoteaf (file));
      ok = false;
    }
  else if (info != FTS_DP)
    {
      bool excluded = excluded_file_name (exclude, file);
      if (! excluded)
        {
          /* Make the stat buffer *SB valid, or fail noisily.  */

          if (info == FTS_NSOK)
            {
              fts_set (fts, ent, FTS_AGAIN);
              MAYBE_UNUSED FTSENT const *e = fts_read (fts);
              affirm (e == ent);
              info = ent->fts_info;
            }

          if (info == FTS_NS || info == FTS_SLNONE)
            {
              error (0, ent->fts_errno, _("cannot access %s"), quoteaf (file));
              return false;
            }

          /* The --one-file-system (-x) option cannot exclude anything
             specified on the command-line.  By definition, it can exclude
             a file or directory only when its device number is different
             from that of its just-processed parent directory, and mu does
             not process the parent of a command-line argument.  */
          if (fts->fts_options & FTS_XDEV
              && FTS_ROOTLEVEL < ent->fts_level
              && fts->fts_dev != sb->st_dev)
            excluded = true;
        }

      if (excluded
          || (! opt_count_all
              && (hash_all || (! S_ISDIR (sb->st_mode) && 1 < sb->st_nlink))
              && ! hash_ins (di_files, sb->st_ino, sb->st_dev)))
        {
          /* If ignoring a directory in preorder, skip its children.
             Ignore the next fts_read output too, as it's a postorder
             visit to the same directory.  */
          if (info == FTS_D)
            {
              fts_set (fts, ent, FTS_SKIP);
              MAYBE_UNUSED FTSENT const *e = fts_read (fts);
              affirm (e == ent);
            }

          return true;
        }

      switch (info)
        {
        case FTS_D:
          return true;

        case FTS_ERR:
          /* An error occurred, but the size is known, so count it.  */
          error (0, ent->fts_errno, "%s", quotef (file));
          ok = false;
          break;

        case FTS_DC:
          /* If not following symlinks and not a (bind) mount point.  */
          if (cycle_warning_required (fts, ent)
              && ! mount_point_in_fts_cycle (ent))
            {
              emit_cycle_warning (file);
              return false;
            }
          return true;
        }
    }

  if (!get_file_cachestat(ent, sb, time_type, &mui)) {
    error (EXIT_FAILURE, errno, "getting file cache stat for %s failed", ent->fts_path);
  }

  level = ent->fts_level;
  mui_to_print = mui;

  if (n_alloc == 0)
    {
      n_alloc = level + 10;
      mulvl = xcalloc (n_alloc, sizeof *mulvl);
    }
  else
    {
      if (level == prev_level)
        {
          /* This is usually the most common case.  Do nothing.  */
        }
      else if (level > prev_level)
        {
          /* Descending the hierarchy.
             Clear the accumulators for *all* levels between prev_level
             and the current one.  The depth may change dramatically,
             e.g., from 1 to 10.  */

          if (n_alloc <= level)
            {
              mulvl = xnrealloc (mulvl, level, 2 * sizeof *mulvl);
              n_alloc = level * 2;
            }

          for (size_t i = prev_level + 1; i <= level; i++)
            {
              muinfo_init (&mulvl[i].ent);
              muinfo_init (&mulvl[i].subdir);
            }
        }
      else /* level < prev_level */
        {
          /* Ascending the hierarchy.
             Process a directory only after all entries in that
             directory have been processed.  When the depth decreases,
             propagate sums from the children (prev_level) to the parent.
             Here, the current level is always one smaller than the
             previous one.  */

          affirm (level == prev_level - 1);

          muinfo_add (&mui_to_print, &mulvl[prev_level].ent);
          if (!opt_separate_dirs)
            muinfo_add (&mui_to_print, &mulvl[prev_level].subdir);
          muinfo_add (&mulvl[level].subdir, &mulvl[prev_level].ent);
          muinfo_add (&mulvl[level].subdir, &mulvl[prev_level].subdir);
        }
    }

  prev_level = level;

  /* Let the size of a directory entry contribute to the total for the
     containing directory, unless --separate-dirs (-S) is specified.  */
  if (! (opt_separate_dirs && IS_DIR_TYPE (info)))
    muinfo_add (&mulvl[level].ent, &mui);

  /* Even if this directory is unreadable or we can't chdir into it,
     do let its size contribute to the total. */
  muinfo_add (&tot_mui, &mui);

  if ((IS_DIR_TYPE (info) && level <= max_depth)
      || (opt_all && level <= max_depth)
      || level == 0)
    {
      /* Print or elide this entry according to the --threshold option.  */
      uintmax_t v = mui_to_print.cache_size;
      if (opt_threshold < 0
          ? v <= -opt_threshold
          : v >= opt_threshold) {
          mu_print_size(&mui_to_print, file, format);
          }
    }

  return ok;
}

/* Recursively print the sizes of the directories (and, if selected, files)
   named in FILES, the last entry of which is null.
   BIT_FLAGS controls how fts works.
   Return true if successful.  */

static bool
mu_files (char **files, int bit_flags, char const *format)
{
  bool ok = true;

  if (*files)
    {
      FTS *fts = xfts_open (files, bit_flags, nullptr);

      while (true)
        {
          FTSENT *ent;

          ent = fts_read (fts);
          if (ent == nullptr)
            {
              if (errno != 0)
                {
                  error (0, errno, _("fts_read failed: %s"),
                         quotef (fts->fts_path));
                  ok = false;
                }

              /* When exiting this loop early, be careful to reset the
                 global, prev_level, used in process_file.  Otherwise, its
                 (level == prev_level - 1) assertion could fail.  */
              prev_level = 0;
              break;
            }
          FTS_CROSS_CHECK (fts);

          ok &= process_file (fts, ent, format);
        }

      if (fts_close (fts) != 0)
        {
          error (0, errno, _("fts_close failed"));
          ok = false;
        }
    }

  return ok;
}

int
main (int argc, char **argv)
{
  char *cwd_only[2];
  bool max_depth_specified = false;
  bool ok = true;
  char *files_from = nullptr;

  /* Bit flags that control how fts works.  */
  int bit_flags = FTS_NOSTAT;

  /* Select one of the three FTS_ options that control if/when
     to follow a symlink.  */
  int symlink_deref_bits = FTS_PHYSICAL;

  /* If true, display only a total for each argument. */
  bool opt_summarize_only = false;

  cwd_only[0] = bad_cast (".");
  cwd_only[1] = nullptr;

  initialize_main (&argc, &argv);
  set_program_name (argv[0]);
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  atexit (close_stdout);

  exclude = new_exclude ();

  human_options (getenv ("MU_BLOCK_SIZE"),
                 &human_output_opts, &output_block_size);

  muinfo_init(&tot_mui);

  char *format = nullptr;

  while (true)
    {
      int oi = -1;
      int c = getopt_long (argc, argv, "0abd:cf:hHklmst:xB:DLPSX:",
                           long_options, &oi);
      if (c == -1)
        break;

      switch (c)
        {
#if DU_DEBUG
        case FTS_DEBUG:
          fts_debug = true;
          break;
#endif

        case '0':
          opt_nul_terminate_output = true;
          break;

        case 'a':
          opt_all = true;
          break;

        case 'b':
          human_output_opts = 0;
          output_block_size = 1;
          break;

        case 'c':
          print_grand_total = true;
          break;

        case 'f':
          format = optarg;
          break;

        case 'h':
          human_output_opts = human_autoscale | human_SI | human_base_1024;
          output_block_size = 1;
          break;

        case HUMAN_SI_OPTION:
          human_output_opts = human_autoscale | human_SI;
          output_block_size = 1;
          break;

        case 'k':
          human_output_opts = 0;
          output_block_size = 1024;
          break;

        case 'd':		/* --max-depth=N */
          {
            intmax_t tmp;
            if (xstrtoimax (optarg, nullptr, 0, &tmp, "") == LONGINT_OK
                && tmp <= IDX_MAX)
              {
                max_depth_specified = true;
                max_depth = tmp;
              }
            else
              {
                error (0, 0, _("invalid maximum depth %s"),
                       quote (optarg));
                ok = false;
              }
          }
          break;

        case 'm':
          human_output_opts = 0;
          output_block_size = 1024 * 1024;
          break;

        case 'l':
          opt_count_all = true;
          break;

        case 's':
          opt_summarize_only = true;
          break;

        case 't':
          {
            enum strtol_error e;
            e = xstrtoimax (optarg, nullptr, 0, &opt_threshold,
                            "kKmMGTPEZYRQ0");
            if (e != LONGINT_OK)
              xstrtol_fatal (e, oi, c, long_options, optarg);
            if (opt_threshold == 0 && *optarg == '-')
              {
                /* Do not allow -0, as this wouldn't make sense anyway.  */
                error (EXIT_FAILURE, 0, _("invalid --threshold argument '-0'"));
              }
          }
          break;

        case 'x':
          bit_flags |= FTS_XDEV;
          break;

        case 'B':
          {
            enum strtol_error e = human_options (optarg, &human_output_opts,
                                                 &output_block_size);
            if (e != LONGINT_OK)
              xstrtol_fatal (e, oi, c, long_options, optarg);
          }
          break;

        case 'H':  /* NOTE: before 2008-12, -H was equivalent to --si.  */
        case 'D':
          symlink_deref_bits = FTS_COMFOLLOW | FTS_PHYSICAL;
          break;

        case 'L': /* --dereference */
          symlink_deref_bits = FTS_LOGICAL;
          break;

        case 'P': /* --no-dereference */
          symlink_deref_bits = FTS_PHYSICAL;
          break;

        case 'S':
          opt_separate_dirs = true;
          break;

        case 'X':
          if (add_exclude_file (add_exclude, exclude, optarg,
                                EXCLUDE_WILDCARDS, '\n'))
            {
              error (0, errno, "%s", quotef (optarg));
              ok = false;
            }
          break;

        case FILES0_FROM_OPTION:
          files_from = optarg;
          break;

        case EXCLUDE_OPTION:
          add_exclude (exclude, optarg, EXCLUDE_WILDCARDS);
          break;

        case TIME_OPTION:
          opt_time = true;
          time_type =
            (optarg
             ? XARGMATCH ("--time", optarg, time_args, time_types)
             : time_mtime);
          localtz = tzalloc (getenv ("TZ"));
          break;

        case TIME_STYLE_OPTION:
          time_style = optarg;
          break;

        case_GETOPT_HELP_CHAR;

        case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

        default:
          ok = false;
        }
    }

  if (!ok)
    usage (EXIT_FAILURE);

  if (opt_all && opt_summarize_only)
    {
      error (0, 0, _("cannot both summarize and show all entries"));
      usage (EXIT_FAILURE);
    }

  if (opt_summarize_only && max_depth_specified && max_depth == 0)
    {
      error (0, 0,
             _("warning: summarizing is the same as using --max-depth=0"));
    }

  if (opt_summarize_only && max_depth_specified && max_depth != 0)
    {
      error (0, 0, _("warning: summarizing conflicts with --max-depth=%td"),
             max_depth);
      usage (EXIT_FAILURE);
    }

  if (opt_summarize_only)
    max_depth = 0;

  /* Process time style if printing last times.  */
  if (opt_time)
    {
      if (! time_style)
        {
          time_style = getenv ("TIME_STYLE");

          /* Ignore TIMESTYLE="locale", for compatibility with ls.  */
          if (! time_style || STREQ (time_style, "locale"))
            time_style = "long-iso";
          else if (*time_style == '+')
            {
              /* Ignore anything after a newline, for compatibility
                 with ls.  */
              char *p = strchr (time_style, '\n');
              if (p)
                *p = '\0';
            }
          else
            {
              /* Ignore "posix-" prefix, for compatibility with ls.  */
              static char const posix_prefix[] = "posix-";
              static const size_t prefix_len = sizeof posix_prefix - 1;
              while (STREQ_LEN (time_style, posix_prefix, prefix_len))
                time_style += prefix_len;
            }
        }

      if (*time_style == '+')
        time_format = time_style + 1;
      else
        {
          switch (XARGMATCH ("time style", time_style,
                             time_style_args, time_style_types))
            {
            case full_iso_time_style:
              time_format = "%Y-%m-%d %H:%M:%S.%N %z";
              break;

            case long_iso_time_style:
              time_format = "%Y-%m-%d %H:%M";
              break;

            case iso_time_style:
              time_format = "%Y-%m-%d";
              break;
            }
        }
    }

  struct argv_iterator *ai;
  if (files_from)
    {
      /* When using --files0-from=F, you may not specify any files
         on the command-line.  */
      if (optind < argc)
        {
          error (0, 0, _("extra operand %s"), quote (argv[optind]));
          fprintf (stderr, "%s\n",
                   _("file operands cannot be combined with --files0-from"));
          usage (EXIT_FAILURE);
        }

      if (! (STREQ (files_from, "-") || freopen (files_from, "r", stdin)))
        error (EXIT_FAILURE, errno, _("cannot open %s for reading"),
               quoteaf (files_from));

      ai = argv_iter_init_stream (stdin);

      /* It's not easy here to count the arguments, so assume the
         worst.  */
      hash_all = true;
    }
  else
    {
      char **files = (optind < argc ? argv + optind : cwd_only);
      ai = argv_iter_init_argv (files);

      /* Hash all dev,ino pairs if there are multiple arguments, or if
         following non-command-line symlinks, because in either case a
         file with just one hard link might be seen more than once.  */
      hash_all = (optind + 1 < argc || symlink_deref_bits == FTS_LOGICAL);
    }

  if (!ai)
    xalloc_die ();

  /* Initialize the set of dev,inode pairs.  */
  di_files = di_set_alloc ();
  if (!di_files)
    xalloc_die ();

  /* If not hashing everything, process_file won't find cycles on its
     own, so ask fts_read to check for them accurately.  */
  if (opt_count_all || ! hash_all)
    bit_flags |= FTS_TIGHT_CYCLE_CHECK;

  bit_flags |= symlink_deref_bits;
  static char *temp_argv[] = { nullptr, nullptr };

  while (true)
    {
      bool skip_file = false;
      enum argv_iter_err ai_err;
      char *file_name = argv_iter (ai, &ai_err);
      if (!file_name)
        {
          switch (ai_err)
            {
            case AI_ERR_EOF:
              goto argv_iter_done;
            case AI_ERR_READ:
              error (0, errno, _("%s: read error"),
                     quotef (files_from));
              ok = false;
              goto argv_iter_done;
            case AI_ERR_MEM:
              xalloc_die ();
            default:
              affirm (!"unexpected error code from argv_iter");
            }
        }
      if (files_from && STREQ (files_from, "-") && STREQ (file_name, "-"))
        {
          /* Give a better diagnostic in an unusual case:
             printf - | du --files0-from=- */
          error (0, 0, _("when reading file names from stdin, "
                         "no file name of %s allowed"),
                 quoteaf (file_name));
          skip_file = true;
        }

      /* Report and skip any empty file names before invoking fts.
         This works around a glitch in fts, which fails immediately
         (without looking at the other file names) when given an empty
         file name.  */
      if (!file_name[0])
        {
          /* Diagnose a zero-length file name.  When it's one
             among many, knowing the record number may help.
             FIXME: currently print the record number only with
             --files0-from=FILE.  Maybe do it for argv, too?  */
          if (files_from == nullptr)
            error (0, 0, "%s", _("invalid zero-length file name"));
          else
            {
              /* Using the standard 'filename:line-number:' prefix here is
                 not totally appropriate, since NUL is the separator, not NL,
                 but it might be better than nothing.  */
              idx_t file_number = argv_iter_n_args (ai);
              error (0, 0, "%s:%td: %s", quotef (files_from),
                     file_number, _("invalid zero-length file name"));
            }
          skip_file = true;
        }

      if (skip_file)
        ok = false;
      else
        {
          temp_argv[0] = file_name;
          ok &= mu_files (temp_argv, bit_flags, format);
        }
    }
 argv_iter_done:

  argv_iter_free (ai);
  di_set_free (di_files);
  if (di_mnt)
    di_set_free (di_mnt);

  if (files_from && (ferror (stdin) || fclose (stdin) != 0) && ok)
    error (EXIT_FAILURE, 0, _("error reading %s"), quoteaf (files_from));

  if (print_grand_total) {
    mu_print_size (&tot_mui, _("total"), format);
  }


  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
