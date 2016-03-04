/* filemode.c -- make a string describing file modes
   Copyright (C) 1985, 1990, 1993, 1998-2000 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include <sys/types.h>
#include <sys/stat.h>

#include "filemode.h"

#if !S_IRUSR
# if S_IREAD
#  define S_IRUSR S_IREAD
# else
#  define S_IRUSR 00400
# endif
#endif

#if !S_IWUSR
# if S_IWRITE
#  define S_IWUSR S_IWRITE
# else
#  define S_IWUSR 00200
# endif
#endif

#if !S_IXUSR
# if S_IEXEC
#  define S_IXUSR S_IEXEC
# else
#  define S_IXUSR 00100
# endif
#endif

#if !S_IRGRP
# define S_IRGRP (S_IRUSR >> 3)
#endif
#if !S_IWGRP
# define S_IWGRP (S_IWUSR >> 3)
#endif
#if !S_IXGRP
# define S_IXGRP (S_IXUSR >> 3)
#endif
#if !S_IROTH
# define S_IROTH (S_IRUSR >> 6)
#endif
#if !S_IWOTH
# define S_IWOTH (S_IWUSR >> 6)
#endif
#if !S_IXOTH
# define S_IXOTH (S_IXUSR >> 6)
#endif

#ifdef STAT_MACROS_BROKEN
# undef S_ISBLK
# undef S_ISCHR
# undef S_ISDIR
# undef S_ISFIFO
# undef S_ISLNK
# undef S_ISMPB
# undef S_ISMPC
# undef S_ISNWK
# undef S_ISREG
# undef S_ISSOCK
#endif /* STAT_MACROS_BROKEN.  */

#if !defined S_ISBLK && defined S_IFBLK
# define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#endif
#if !defined S_ISCHR && defined S_IFCHR
# define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#endif
#if !defined S_ISDIR && defined S_IFDIR
# define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#if !defined S_ISREG && defined S_IFREG
# define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#if !defined S_ISFIFO && defined S_IFIFO
# define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#endif
#if !defined S_ISLNK && defined S_IFLNK
# define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#endif
#if !defined S_ISSOCK && defined S_IFSOCK
# define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif
#if !defined S_ISMPB && defined S_IFMPB /* V7 */
# define S_ISMPB(m) (((m) & S_IFMT) == S_IFMPB)
# define S_ISMPC(m) (((m) & S_IFMT) == S_IFMPC)
#endif
#if !defined S_ISNWK && defined S_IFNWK /* HP/UX */
# define S_ISNWK(m) (((m) & S_IFMT) == S_IFNWK)
#endif
#if !defined S_ISDOOR && defined S_IFDOOR /* Solaris 2.5 and up */
# define S_ISDOOR(m) (((m) & S_IFMT) == S_IFDOOR)
#endif
#if !defined S_ISCTG && defined S_IFCTG /* MassComp */
# define S_ISCTG(m) (((m) & S_IFMT) == S_IFCTG)
#endif



/* Set the 's' and 't' flags in file attributes string CHARS,
   according to the file mode BITS.  */

static void
setst (mode_t bits, char *chars)
{
#ifdef S_ISUID
  if (bits & S_ISUID)
    {
      if (chars[3] != 'x')
	/* Set-uid, but not executable by owner.  */
	chars[3] = 'S';
      else
	chars[3] = 's';
    }
#endif
#ifdef S_ISGID
  if (bits & S_ISGID)
    {
      if (chars[6] != 'x')
	/* Set-gid, but not executable by group.  */
	chars[6] = 'S';
      else
	chars[6] = 's';
    }
#endif
#ifdef S_ISVTX
  if (bits & S_ISVTX)
    {
      if (chars[9] != 'x')
	/* Sticky, but not executable by others.  */
	chars[9] = 'T';
      else
	chars[9] = 't';
    }
#endif
}

/* Return a character indicating the type of file described by
   file mode BITS:
   'd' for directories
   'D' for doors
   'b' for block special files
   'c' for character special files
   'n' for network special files
   'm' for multiplexor files
   'M' for an off-line (regular) file
   'l' for symbolic links
   's' for sockets
   'p' for fifos
   'C' for contigous data files
   '-' for regular files
   '?' for any other file type.  */

static char
ftypelet (mode_t bits)
{
#ifdef S_ISBLK
  if (S_ISBLK (bits))
    return 'b';
#endif
  if (S_ISCHR (bits))
    return 'c';
  if (S_ISDIR (bits))
    return 'd';
  if (S_ISREG (bits))
    return '-';
#ifdef S_ISFIFO
  if (S_ISFIFO (bits))
    return 'p';
#endif
#ifdef S_ISLNK
  if (S_ISLNK (bits))
    return 'l';
#endif
#ifdef S_ISSOCK
  if (S_ISSOCK (bits))
    return 's';
#endif
#ifdef S_ISMPC
  if (S_ISMPC (bits))
    return 'm';
#endif
#ifdef S_ISNWK
  if (S_ISNWK (bits))
    return 'n';
#endif
#ifdef S_ISDOOR
  if (S_ISDOOR (bits))
    return 'D';
#endif
#ifdef S_ISCTG
  if (S_ISCTG (bits))
    return 'C';
#endif

  /* The following two tests are for Cray DMF (Data Migration
     Facility), which is a HSM file system.  A migrated file has a
     `st_dm_mode' that is different from the normal `st_mode', so any
     tests for migrated files should use the former.  */

#ifdef S_ISOFD
  if (S_ISOFD (bits))
    /* off line, with data  */
    return 'M';
#endif
#ifdef S_ISOFL
  /* off line, with no data  */
  if (S_ISOFL (bits))
    return 'M';
#endif
  return '?';
}

/* Like filemodestring, but only the relevant part of the `struct stat'
   is given as an argument.  */

void
mode_string (mode_t mode, char *str)
{
  str[0] = ftypelet (mode);
  str[1] = mode & S_IRUSR ? 'r' : '-';
  str[2] = mode & S_IWUSR ? 'w' : '-';
  str[3] = mode & S_IXUSR ? 'x' : '-';
  str[4] = mode & S_IRGRP ? 'r' : '-';
  str[5] = mode & S_IWGRP ? 'w' : '-';
  str[6] = mode & S_IXGRP ? 'x' : '-';
  str[7] = mode & S_IROTH ? 'r' : '-';
  str[8] = mode & S_IWOTH ? 'w' : '-';
  str[9] = mode & S_IXOTH ? 'x' : '-';
  setst (mode, str);
}