/* Reed 5.4 - An autoscrolling text pager
   Copyright (C)2000-2002 Joe Wreschnig <piman@sacredchao.net>

   next_bracket() Copyright (C)2000 Ben Zeigler, 2002 Joe Wreschnig
   getuser(), getgroup() Copyright (C)1985, 1990, 1993, 1998-2000
                                      Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* Dar Williams rocks my world. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>

#include <ctype.h>
#include <curses.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filemode.h"

/*#define strcoll(s1, s2) strcmp(s1, s2)
  /* ^- Uncomment that if you are on a braindead system. */

#define free_regexp(n) if (n) { regfree(n); free(n);  n = NULL; }
#define alert() if (cue) beep();

#define VERSION "5.4"

/* Return values for activate_bufer */
#define QUIT            0 /* Exit Reed */
#define NEXT_FILE       1 /* Go to the next buffer */
#define PREV_FILE       2 /* "       " previous buffer */
#define DELETE_FILE     3 /* Delete the current buffer */
#define LOAD_FILE       4 /* Load a new buffer */
#define RELOAD_FILE     5 /* Reload the current buffer and respace it */
#define BOOKMARKS       6 /* Load the bookmarks file */
#define HELP            7 /* Load the help file */
#define LOAD_INLINE     8 /* Try and open a filename on the current line */
#define LOAD_LIST	9 /* Display a list of open buffers */

/* Some preset buffer names that get compared a lot */
#define B_STDIN     "Input Stream"
#define B_BOOKMARKS "Your Bookmarks"
#define B_BUFFERS   "Buffer List"

typedef struct _Buffer Buffer;

static WINDOW *text = NULL, *status = NULL;
static char *bookmark_file = NULL, *inline_file = NULL,
  *inline_mark_name = NULL, cue = 1, initial_pause = 1;
static long int initial_delay = 500000;
static short int initial_jump = 1;

static struct userid *user_alist;
static struct userid *group_alist;

struct _Buffer {
  int line, lines, *markers;
  signed short jump;
  unsigned long delay;	/* Stored as microseconds/10 */
  unsigned char *filename, *message, is_file, paused;
  FILE *file;
  regex_t *match; /* The current search */
  Buffer *n, *p;
};

struct userid {
  union {
    uid_t u;
    gid_t g;
  } id;
  char *name;
  struct userid *next;
};

void *xmalloc(size_t size) {
  void *value = malloc(size);
  if (value == NULL) {
    perror("reed");
    exit(1);
  }
  return value;
}

/* Prototypes. Lots of them. */
Buffer *new_buffer(void);
void delete_buffer(Buffer *buf);
Buffer *next_buffer(Buffer *buf);
Buffer *prev_buffer(Buffer *buf);
void replace_buffer(Buffer *from, Buffer *to);

Buffer *load_file(const char *filename);
void find_line_markers(Buffer *buf);

void pause_file(Buffer *buf);
void unpause_file(Buffer *buf);
void toggle_pause(Buffer *buf);

int seek_to_line_and_display(Buffer *buf, int line);
void update_status_bar(Buffer *buf);

char *input_string(const char *message);
float input_decimal(const char *message);
int input_integer(const char *message);

int get_bookmark(const char *name, Buffer *buf);
void delete_bookmark(const char *name, Buffer *buf);
void set_bookmark(const char *name, Buffer *buf);
void clear_bookmarks(Buffer *buf);
void set_char_bookmark(char c, Buffer *buf);
int get_char_bookmark(char c, Buffer *buf);

regex_t *make_regexp(const char *s);
int search_for(regex_t *match, Buffer *buf, int up_down);
void new_search(Buffer *buf, int up_down);
void next_bracket(const char *left, const char *right, Buffer *buf,
                  int direction);

int activate_buffer(Buffer *buf);

Buffer *load_new_buffer(Buffer *old_buf, const char *filename);
void parse_rc_file(void);

void initialize_windows(void);
void spawn_editor(Buffer *buf);

char *getuser (uid_t uid);
char *getgroup (gid_t gid);

void print_help();
void print_version();

void parse_stdin(Buffer *n);
Buffer *load_buffer_list(Buffer *buf);
Buffer *load_help_file(Buffer *active);
Buffer *load_bookmarks(Buffer *active);

void find_inline_filename(Buffer *n);
char *extract_filename(const char *line);

/* Allocate the memory for a new buffer, and give it some nice defaults */
Buffer *new_buffer() {
  Buffer *buf = xmalloc(sizeof(Buffer));
  buf->lines = buf->line = 0;
  buf->paused = initial_pause;
  buf->is_file = 1;
  buf->delay = initial_delay;
  buf->jump = initial_jump;
  buf->n = buf->p = NULL;
  buf->file = NULL;
  buf->filename = buf->message = NULL;
  buf->markers = NULL;
  buf->match = NULL;
  return buf;
}

/* Delete a buffer, free all memory associated with it, and fix the list */
void delete_buffer(Buffer *buf) {
  if (buf->n) buf->n->p = buf->p;
  if (buf->p) buf->p->n = buf->n;
  if (buf->match) regfree(buf->match);
  if (buf->file) fclose(buf->file);
  free(buf->match);
  free(buf->filename);
  free(buf->markers);
  free(buf);
}

Buffer *next_buffer(Buffer *buf) {
  if (buf->n != NULL) return buf->n;
  buf->message = "You are at the last buffer.";
  return buf;
}

Buffer *prev_buffer(Buffer *buf) {
  if (buf->p != NULL && strcoll(buf->p->filename, B_BUFFERS)) return buf->p;
  return load_buffer_list(buf);
}

int one(struct dirent *unused) { return 1; }

/* From the GNU fileutils package */
char *getuser (uid_t uid) {
  struct userid *tail;
  struct passwd *pwent;
  for (tail = user_alist; tail; tail = tail->next)
    if (tail->id.u == uid)
      return tail->name;
  pwent = getpwuid(uid);
  tail = (struct userid *)xmalloc(sizeof(struct userid));
  tail->id.u = uid;
  tail->name = pwent ? strdup (pwent->pw_name) : NULL;
  /* Add to the head of the list, so most recently used is first.  */
  tail->next = user_alist;
  user_alist = tail;
  return tail->name;
}

/* From the GNU fileutils package */
char *getgroup (gid_t gid) {
  struct userid *tail;
  struct group *grent;
  for (tail = group_alist; tail; tail = tail->next)
    if (tail->id.g == gid)
      return tail->name;
  grent = getgrgid (gid);
  tail = (struct userid *)xmalloc(sizeof(struct userid));
  tail->id.g = gid;
  tail->name = grent ? strdup (grent->gr_name) : NULL;
  /* Add to the head of the list, so most recently used is first.  */
  tail->next = group_alist;
  group_alist = tail;
  return tail->name;
}

/* Create a nice-looking file list for viewing directories */
FILE *display_directory(const char *filename) {
  DIR *dir = opendir(filename);
  FILE *file = tmpfile();
  struct dirent **entry;
  struct stat st;
  char mode[11], stime[13];
  int n, cnt;
  mode[10] = '\0';
  chdir(filename);
  fprintf(file,"Mode           User    Group       Size Modified     Name\n");
  fprintf(file,
          "-----------------------------------------------------------\n");
  n = scandir(filename, &entry, (void*)one, alphasort);
  for (cnt = 0; cnt < n; ++cnt) { /* GNU libc info files */
    stat(entry[cnt]->d_name, &st);
    mode_string(st.st_mode, mode);
    strftime(stime, 15, "%b %e %H:%M", localtime(&st.st_mtime));
    fprintf(file, "%s %8s %8s %10d %s %s\n", mode, getuser(st.st_uid),
            getgroup(st.st_gid), (int)st.st_size, stime, entry[cnt]->d_name);
    free(entry[cnt]);
  }
  free(entry);
  closedir(dir);
  return file;
}

/* Copy stdin to a new internal buffer, and reopen it to be the tty */
void parse_stdin(Buffer *n) {
  char line[1024], section[2], man[64];
  n->is_file = 0;
  n->file = tmpfile();
  fgets(line, 1024, stdin);
  if (sscanf(line, "%63[^( ](%1[0-9]) %*s", man, section) == 2) {
    int i;
    n->filename = xmalloc(sizeof(char) * 128);
    for (i = 0; i < strlen(man); i++) man[i] = tolower(man[i]);
    snprintf(n->filename, 128, "%s(%s)", man, section);
  } else n->filename = strdup(B_STDIN);
  fputs(line, n->file);
  while (fgets(line, 1024, stdin)) fputs(line, n->file);
  freopen("/dev/tty", "r", stdin);
}

/* Make a buffer that's a list of the rest of the buffers */
Buffer *load_buffer_list(Buffer *buf) {
  Buffer *new_buf = new_buffer();
  while (buf->p != NULL) buf = buf->p;
  new_buf->filename = strdup(B_BUFFERS);
  new_buf->file = tmpfile();
  new_buf->is_file = 0;
  if (!strcoll(buf->filename, B_BUFFERS)) {
    buf = buf->n;
    delete_buffer(buf->p);
  }
  buf->p = new_buf;
  new_buf->n = buf;
  for (; buf != NULL; buf = buf->n) {
    int i;
    fprintf(new_buf->file, "%s\t", buf->filename);
    for (i = ((strlen(buf->filename) / 8) * 8) + 8; i != 60; i++)
      fputc(' ', new_buf->file);
    fprintf(new_buf->file, "%d/%d\n", buf->line, buf->lines);
  }
  rewind(new_buf->file);
  find_line_markers(new_buf);
  return new_buf;
}

Buffer *load_help_file(Buffer *active) {
  Buffer *new_buf =
    load_new_buffer(active, "/usr/local/share/reed/help");
  if (new_buf == active)
    new_buf = load_new_buffer(active, "/usr/share/reed/help");
  if (new_buf != active) {
    active->message = NULL;
    active = new_buf;
  } else {
    active->message = "The help file was not found.";
  }
  return active;
}

Buffer *load_bookmarks(Buffer *active) {
  Buffer *buf = active;
  while (buf->p != NULL) buf = buf->p;

  for (; buf != NULL; buf = buf->n) {
    if (!strcoll(buf->filename, B_BOOKMARKS)) {
      active = load_new_buffer(buf, bookmark_file);
      free(active->filename);
      active->is_file = 0;
      active->filename = strdup(B_BOOKMARKS);
      delete_buffer(buf);
      return active;
    }
  }

  active = load_new_buffer(active, bookmark_file);
  free(active->filename);
  active->is_file = 0;
  active->filename = strdup(B_BOOKMARKS);
  return active;
}

/* Create a new buffer from a file on disk */
Buffer *load_file(const char *filename) {
  Buffer *buf = new_buffer();
  struct stat st;
  char *newfilename = NULL;
  buf->filename = xmalloc(sizeof(char) * (PATH_MAX + 1));
  if (strcoll(filename, "-")) { /* Not stdin... */
    if (strstr(filename, "~/") == filename) {
      newfilename = xmalloc(sizeof(char) * (strlen(filename) +
                                            strlen(getenv("HOME"))));
      sprintf(newfilename, "%s/%s", getenv("HOME"), filename + 2);
    } else newfilename = strdup(filename);
    if (realpath(newfilename, buf->filename) == NULL) {
      delete_buffer(buf);
      free(newfilename);
      return NULL;
    }
    buf->filename = realloc(buf->filename,
                             sizeof(char) * (strlen(buf->filename) + 1));
    stat(buf->filename, &st);
    if (S_ISDIR(st.st_mode)) buf->file = display_directory(buf->filename);
    else buf->file = fopen(buf->filename, "r");

    if (buf->file == NULL) {
      free(buf->filename);
      free(buf);
      free(newfilename);
      return NULL;
    }
  } else parse_stdin(buf);

  rewind(buf->file);
  free(newfilename);
  return buf;
}

/* Find the line breaks on \n or COLS characters, and fill out the
   markers array in the node. */
void find_line_markers(Buffer *buf) {
  unsigned int length = 100, i;
  buf->lines = buf->line = 0;
  if (buf->markers != NULL) free(buf->markers);
  buf->markers = xmalloc(sizeof(int) * length);
  buf->markers[0] = 0;
  rewind(buf->file);

  while (!feof(buf->file)) {
    for (i = 0; i < COLS; i++) {
      switch (fgetc(buf->file)) {
      case '\b': i -= 2; break;
      case '\n': i = COLS - 1; break;
      case '\t': i = (((i / 8) + 1) * 8) - 1; break;
      case '\0': case '\a': case 255: i--; break;
      }
    }
    buf->lines++;
    if (buf->lines == length) {
      length *= 2;
      buf->markers = realloc(buf->markers, sizeof(int) * length);
    }
    buf->markers[buf->lines] = ftell(buf->file);
  }
  buf->markers = realloc(buf->markers, sizeof(int) * (buf->lines + 1));
}

void pause_file(Buffer *buf) {
  buf->paused = 1;
  update_status_bar(buf);
  nodelay(text, FALSE);
}

void unpause_file(Buffer *buf) {
  buf->paused = 0;
  update_status_bar(buf);
  nodelay(text, TRUE);
}

void toggle_pause(Buffer *buf) {
  buf->paused == 1 ? unpause_file(buf) : pause_file(buf);
}

/* Deal with all the nasty bits of actually displaying a line of the
   file, with formatting, highlighted search, and so on. */
int seek_to_line_and_display(Buffer *buf, int line) {
  int i;

  /* Scrolled too far down. */
  if (line > (buf->lines - 1)) {
    line = (buf->lines - 1);
    if (buf->jump > 0) pause_file(buf);
    alert();
  }

  /* Too far up. */
  if (line < 0) {
    line = 0;
    if (buf->lines > (LINES - 3)) alert();
    wclear(text);
  }

  fseek(buf->file, buf->markers[line], SEEK_SET);

  for (i = 0; i < LINES - 4 && line + i + 1 < buf->lines; i++) {
    unsigned char l[buf->markers[line + i + 1] -
                    buf->markers[line + i] + 1];
    char *dos_style_newline = NULL;
    regmatch_t matched[5];
    int j;

    fgets(l, (int)(buf->markers[line + i + 1] -
                   buf->markers[line + i]) + 1, buf->file);

    dos_style_newline = strstr(l, "\r\n");
    if (dos_style_newline != NULL) dos_style_newline[0] = ' ';

    matched[0].rm_eo = matched[0].rm_so = -1;
    if (buf->match) regexec(buf->match, l, 5, matched, 0);
    for (j = 0; j < strlen(l); j++) {
      if (j >= matched[0].rm_so && j < matched[0].rm_eo)
	wattron(text, A_STANDOUT);

      if (l[j] == '\b') {
	waddch(text, '\b');
	if (l[j - 1] == l[j + 1]) wattron(text, A_BOLD);
	else if (l[j - 1] == '_') wattron(text, A_UNDERLINE);
	waddch(text, l[++j]);
      } else waddch(text, l[j]);
      wattroff(text, A_BOLD | A_UNDERLINE | A_STANDOUT);
    }
  }
  for (; i < LINES - 4; i++) waddstr(text, "\n");
  wrefresh(text);
  return line;
}

void update_status_bar(Buffer *buf) {
  char message[256], *paused = "";

  mvwhline(status, 0, 0, ' ', COLS * 2);

  if (buf->message == NULL) {
    mvwaddstr(status, 0, 0, "File: ");
    if (strlen(buf->filename) > COLS - 6) {
      waddstr(status, "[...]"); /* Reverse truncate the filename
				   (the last characters are more likely to
				   be unique than the first) */
      waddstr(status, buf->filename + (strlen(buf->filename) - COLS + 11));
    } else waddstr(status, buf->filename);
  } else mvwaddstr(status, 0, 0, buf->message);

  if (buf->paused) paused = "Paused. ";
  snprintf(message, 256, "%sDelay: %.1f, Jump: %d. %d/%d, %.1f%%",
	   paused, ((float) buf->delay) / 100000, buf->jump, buf->line,
	   buf->lines, (float)(buf->line) / (float)(buf->lines) * 100.0);
  mvwaddstr(status, 1, COLS - strlen(message), message);
  wrefresh(status);
}

/* Input a string from the status bar, with a prompt */
char *input_string(const char *message) {
  char *value = xmalloc(sizeof(char) * 128);
  mvwhline(status, 0, 0, ' ', COLS);
  echo();
  curs_set(1);
  waddstr(status, message);
  wgetnstr(status, value, 128);
  noecho();
  curs_set(0);
  return value;
}

/* Same for a character */
char input_char(const char *message) {
  char c = ERR;
  mvwhline(status, 0, 0, ' ', COLS);
  waddstr(status, message);
  c = wgetch(status);
  /* If this isn't done, weird things happen, e.g. pressing right arrow
     generates ^[[C, which then executes the C command and (to the user's
     surprise) clears all bookmarks. */
  nodelay(status, TRUE);
  while (wgetch(status) != ERR) {}
  nodelay(status, FALSE);
  return c;
}

/* And a floating point number... */
float input_decimal(const char *message) {
  char *tmp = input_string(message);
  float value = atof(tmp);
  free(tmp);
  return value;
}

/* And an integer. */
int input_integer(const char *message) {
  return (int)(input_decimal(message));
}

/* Return the line number of a bookmark for the current file, or -1 if
   the bookmark isn't found. */
int get_bookmark(const char *name, Buffer *buf) {
  char line[1024], file[PATH_MAX + 1], markname[128];
  int lineno = 0;
  FILE *bf = fopen(bookmark_file, "r");
  if (bf == NULL) return -1;

  while (fgets(line, 1024, bf)) {
    if (sscanf(line, "%[^\t]\t%d\t%127s\n", file, &lineno, markname) == 3) {
      if (strcoll(file, buf->filename) == 0 && strcoll(markname, name) == 0) {
	fclose(bf);
	return lineno;
      }
    }
  }
  fclose(bf);
  return -1;
}

/* Delete a specific bookmark in a file. */
void delete_bookmark(const char *name, Buffer *buf) {
  char line[1024], tmp[strlen(getenv("HOME")) + 14];
  FILE *bf = fopen(bookmark_file, "r"), *tmpf = NULL;
  sprintf(tmp, "%s/.reed_XXXXXX", getenv("HOME"));
  tmpf = fdopen(mkstemp(tmp), "a");
  if (tmpf == NULL || bf == NULL) {
    if (tmpf != NULL) fclose(tmpf);
    if (bf != NULL) fclose(bf);
    return;
  }
  while (fgets(line, 1024, bf)) {
    char file[PATH_MAX + 1], markname[128];
    if (sscanf(line, "%[^\t]\t%*[0-9]\t%127s\n", file, markname) == 2 &&
	(strcoll(markname, name) || strcoll(file, buf->filename)))
      fputs(line, tmpf);
  }
  fclose(bf); fclose(tmpf);
  unlink(bookmark_file);
  rename(tmp, bookmark_file);
}

/* Set a bookmark in a file, deleting any other bookmark of the same name
   first. */
void set_bookmark(const char *name, Buffer *buf) {
  FILE *bf;
  int i;
  if (get_bookmark(name, buf) != -1) delete_bookmark(name, buf);
  bf = fopen(bookmark_file, "a");
  if (bf == NULL) return;
  fprintf(bf, "%s", buf->filename);
  for (i = 48 - strlen(buf->filename); i > 0; i -= 8) fputc('\t', bf);
  fputc('\t', bf);
  fprintf(bf, "%d\t%s\n", buf->line, name);
  fclose(bf);
}

/* Delete all bookmarks in the current file. */
void clear_bookmarks(Buffer *buf) {
  char line[PATH_MAX + 140], tmp[strlen(getenv("HOME")) + 14];
  FILE *bf = fopen(bookmark_file, "r"), *tmpf;
  sprintf(tmp, "%s/.reed_XXXXXX", getenv("HOME"));
  tmpf = fdopen(mkstemp(tmp), "a");
  if (bf) {
    while (fgets(line, PATH_MAX + 140, bf)) {
      char *original = strdup(line);
      char *file = strtok(line, "\t");
      if (strcoll(file, buf->filename)) fprintf(tmpf, original);
      free(original);
    }
    fclose(bf);
  }
  unlink(bookmark_file);
  rename(tmp, bookmark_file);
  fclose(tmpf);
}

/* Set/get single-character bookmarks */
void set_char_bookmark(char c, Buffer *buf) {
  char s[2] = " "; s[0] = c;
  set_bookmark(s, buf);
}

int get_char_bookmark(char c, Buffer *buf) {
  char s[2] = " "; s[0] = c;
  return get_bookmark(s, buf);
}

regex_t *make_regexp(const char *s) {
  regex_t *match = xmalloc(sizeof(regex_t));
  if (regcomp(match, s, 0))
    return NULL;
  return match;
}

int search_for(regex_t *match, Buffer *buf, int up_down) {
  int j, to, found = -1;
  if (match == NULL)
    return buf->line;

  if (up_down == 1) {
    fseek(buf->file, buf->markers[buf->line + 1], SEEK_SET);
    j = buf->line + 1;
    to = buf->lines;
  } else {
    fseek(buf->file, buf->markers[0], SEEK_SET);
    j = 0;
    to = buf->line;
  }
  for (; j < to; j++) {
    int num = buf->markers[j+1] - buf->markers[j];
    char line[num + 1];
    if (fgets(line, num+1, buf->file)) {
      if (index(line, '\b')) { /* Remove formatting codes */
	char *c = strdup(line);
	int i, j = 0;
	for (i = 0; i <= strlen(c); i++)
	  if (c[i] == '\b') j--;
	  else line[j++] = c[i];
	free(c);
      }
      if (!(regexec(match, line, 0, NULL, 0))) {
	found = j;
	if (up_down == 1) break;
      }
    } else break;
  }
  return found;
}

/* Start a new search, freeing any memory related to an old one */
void new_search(Buffer *buf, int up_down) {
  char *s = input_string("Search for: ");
  int new_line;

  if (!strcoll(s, "")) {
    free(s);
    buf->message = "Search cancelled.";
    free_regexp(buf->match);
    return;
  } else if (buf->match != NULL) {
    free_regexp(buf->match);
  }

  if ((buf->match = make_regexp(s)) == NULL) {
    alert();
    buf->message = "Error: Bad regular expression.";
    free(s);
    return;
  }
  buf->line -= up_down;
  new_line = search_for(buf->match, buf, up_down);
  if (new_line >= 0) {
    buf->line = seek_to_line_and_display(buf, new_line);
  } else if (new_line == -1) {
    buf->line += up_down;
    buf->message = "The search pattern was not found.";
    buf->line = seek_to_line_and_display(buf, buf->line);
    free_regexp(buf->match);
  }
  free(s);
  pause_file(buf);
}

void spawn_editor(Buffer *buf) {
  char *editor, *cline;
  if (getenv("VISUAL")) { editor = getenv("VISUAL"); }
  else if (getenv("EDITOR")) { editor = getenv("EDITOR"); }
  else { editor = "/bin/ed"; }
  cline = xmalloc(sizeof(char) *
		  (strlen(editor) + strlen(buf->filename) + 16));
  sprintf(cline, "%s +%d \"%s\"", editor, buf->line, buf->filename);
  system(cline);
  free(cline);
}

/* Check the current buffer against a list of known "openable" buffer list
   types, and if that fails, try to find something anyway. */
void find_inline_filename(Buffer *buf) {
  char l[buf->markers[buf->line + 1] - buf->markers[buf->line] + 1];
  fseek(buf->file, buf->markers[buf->line], SEEK_SET);
  fgets(l, (int)(buf->markers[buf->line + 1] -
		 buf->markers[buf->line]) + 1, buf->file);
  if (!strcoll(buf->filename, B_BOOKMARKS)) { /* Bookmarks file */
    inline_file = strdup(strtok(l, "\t"));
    strtok(NULL, " \t");
    inline_mark_name = strdup(strtok(NULL, "\n"));
  } else if (!strcoll(buf->filename, B_BUFFERS)) /* Buffer list */
    inline_file = strdup(strtok(l, "\t"));
  else { /* Some file that might have a filename */
    struct stat st;
    stat(buf->filename, &st);
    if (S_ISDIR(st.st_mode)) {
      int i = 0;
      strtok(l, " \t");
      for (i = 0; i < 6; i++) strtok(NULL, " \t");
      inline_file = strtok(NULL, "\n");
      if (inline_file != NULL) {
	char *resolved_name = (char *)xmalloc(strlen(inline_file) +
					      strlen(buf->filename) + 3);
	sprintf(resolved_name, "%s/%s", buf->filename, inline_file);
	inline_file = resolved_name;
      }
    } else {
      inline_file = extract_filename(l);
      if (inline_file == NULL)
        buf->message = "No filename was found on the current line.";
    }
  }
}

/* Try to extract a filename by checking each word in the line.
   This is probably slow, especially over NFS. */
char *extract_filename(const char *line) {
  char *working = strdup(line), *retval = NULL;
  char *ptr = strchr(working, '/');
  if (ptr) {
    int i = 0;
    while (ptr[i] != ' ' && ptr[i] != '\n' && ptr[i] != '\0') i++;
    ptr[i] = '\0';
    retval = strdup(ptr);
  } else {
    struct stat st;
    char *s;
    int v = 0;
    for (s = strtok(working, " \t\n"); s != NULL && retval == NULL;
	 s = strtok(NULL, " \t\n")) {
      v = stat(s, &st);
      if (v != -1) retval = strdup(s);
    }
  }
  free(working);
  return retval;
}


/* FIXME: This could probably be done a lot better. */
/* I'm not going to bother documenting this, since if you don't understand
   it but want to make it work better, just write a new one. :P */
void next_bracket(const char *left, const char *right, Buffer *buf,
                  int direction) {
  regex_t *left_match = NULL, *right_match = NULL;
  int left_pos, right_pos, found = -1, old_pos = buf->line;
  left_match = make_regexp(left);
  right_match = make_regexp(right);
  if (direction == 1) {
    buf->line--;
    left_pos = search_for(left_match, buf, 1);
    if (left_pos == -1) {
      buf->message = "Error: The start character was not found.";
      free_regexp(left_match);
      free_regexp(right_match);
      return;
    }
    right_pos = left_pos;
  } else {
    buf->line++;
    right_pos = search_for(right_match, buf, -1);
    if (right_pos == -1) {
      buf->message = "Error: The start parenthesis was not found.";
      buf->line = old_pos;
    }
    left_pos = right_pos;
  }

  while (found == -1) {
      buf->line = left_pos;
      left_pos = search_for(left_match, buf, direction);
      buf->line = right_pos;
      right_pos = search_for(right_match, buf, direction);

      if ((right_pos == -1 && direction == 1) ||
          (left_pos == -1 && direction == -1)) {
        buf->message = "No matching parenthesis was found.";
        found = old_pos;
      }
      else if (((right_pos < left_pos || left_pos == -1) && direction == 1) ||
               ((left_pos > right_pos || right_pos == -1) && direction == -1)) {
        if (direction == 1) found = right_pos;
        if (direction == -1) found = left_pos;
      }
    }
    free_regexp(right_match);
    free_regexp(left_match);
    buf->line = seek_to_line_and_display(buf, found);
}


/* Set the currently displayed file buffer, and do the main input loop,
   and the scrolling loop. */
int activate_buffer(Buffer *buf) {
  unsigned long count = 0;
  int c;
  buf->line = seek_to_line_and_display(buf, buf->line);
  buf->paused ? pause_file(buf) : unpause_file(buf);
  update_status_bar(buf);
  while ((c = wgetch(text)) != 'q') {
    buf->message = NULL;

    switch (c) {
    case ERR: break;
    case KEY_UP:
    case 'k':
      buf->line = seek_to_line_and_display(buf, buf->line - 1);
      break;
      
    case KEY_DOWN:
    case 'e':
    case 'j':
      buf->line = seek_to_line_and_display(buf, buf->line + 1);
      break;
      
    case KEY_NPAGE:
    case ' ':
    case 'z':
      buf->line =
	seek_to_line_and_display(buf, buf->line + (LINES - 4));
      break;
      
    case KEY_PPAGE:
    case 'w':
    case 'b':
      buf->line =
	seek_to_line_and_display(buf, buf->line - (LINES - 4));
      break;
      
    case 'u':
      buf->line =
	seek_to_line_and_display(buf, buf->line - (LINES - 4) / 2);
      break;
      
    case 'd':
      buf->line =
	seek_to_line_and_display(buf, buf->line + (LINES - 4) / 2);
      break;
      
    case 'p':
      toggle_pause(buf);
      break;
      
    case '\r':
    case '\n':
      if (buf->paused)
        buf->line = seek_to_line_and_display(buf, buf->line + 1);
      else count = buf->delay;
      break;
      
    case '-':
    case '_':
      if (buf->delay > 100000)
	buf->delay -= 20000;
      else
	buf->delay /= 2;
      break;
      
    case '+':
    case '=':
      buf->delay += 20000;
      break;
      
    case '>':
    case '.':
    case 'G':
    case KEY_END:
      buf->line = seek_to_line_and_display(buf, buf->lines - (LINES - 3));
	    break;
	    
    case '<':
    case ',':
    case 'g':
    case KEY_HOME:
      buf->line = seek_to_line_and_display(buf, 0);
      break;
	    
    case 's':
      buf->delay =
	input_decimal("Enter the time to wait between lines: ") * 100000;
      break;

    case 'J':
      buf->jump =
        input_integer("Enter the number of lines to skip at a time: ");
      break;

    case 'm':{
      char *s = input_string("Go to bookmark: ");
      int new_line = 0;
      if (s != NULL) new_line = get_bookmark(s, buf);
      if (new_line == -1) {
	alert();
	buf->message = "The bookmark was not found.";
      } else buf->line = seek_to_line_and_display(buf, new_line);
      pause_file(buf);
      free(s);
      break;
    }
      
    case 'M':{
      char *s = input_string("Set bookmark: ");
      if (strlen(s)) set_bookmark(s, buf);
      free(s);
      break;
    }
      
    case 'D':{
      char *s = input_string("Delete bookmark: ");
      delete_bookmark(s, buf);
      free(s);
      break;
    }

    case 'C': clear_bookmarks(buf); break;
    case 'B': return BOOKMARKS;

    case '\'':{
      int new_line = get_char_bookmark(input_char("Go to "), buf);
      if (new_line == -1) buf->message = "The bookmark was not found.";
      else buf->line = seek_to_line_and_display(buf, new_line);
      pause_file(buf);
      break;
    }

    case '"':
      set_char_bookmark(input_char("Set "), buf);
      break;

    case 'l':{
      int new_line = get_bookmark("default", buf);
      if (new_line == -1) {
        alert();
        buf->message = "The bookmark was not found.";
      } else buf->line = seek_to_line_and_display(buf, new_line);
      pause_file(buf);
      break;
    }

    case 'L':
      set_bookmark("default", buf);
      break;

    case '/': new_search(buf, 1); break;
    case 'n':{
      int new_line = search_for(buf->match, buf, 1);
      if (new_line != -1)
        buf->line = seek_to_line_and_display(buf, new_line);
      break;
    }
    case '?': new_search(buf, -1); break;
    case 'N':{
      int new_line = search_for(buf->match, buf, -1);
      if (new_line != -1)
        buf->line = seek_to_line_and_display(buf, new_line);
      break;
    }

    case '(': next_bracket("(",")", buf, 1); break;
    case '{': next_bracket("{","}", buf, 1); break;
    case '[': next_bracket("\\[","\\]", buf, 1); break;
    case ')': next_bracket("(",")", buf, -1); break;
    case '}': next_bracket("{","}", buf, -1); break;
    case ']': next_bracket("\\[","\\]", buf, -1); break;

    case 'F':
      free_regexp(buf->match);
      buf->line = seek_to_line_and_display(buf, buf->line);
      break;

    case '!':{
      char *s = input_string("Run command: ");
      endwin();
      system(s);
      free(s);
      initialize_windows();
      buf->line = seek_to_line_and_display(buf, buf->line);
      break;
    }

    case '%':
      buf->line = seek_to_line_and_display(buf,
        buf->lines * (input_decimal("Go to percentage: ") / 100.0));
      break;

    case 'v':
      endwin();
      if (buf->is_file) spawn_editor(buf);
      else buf->message = "This buffer is not an editable file.";
      initialize_windows();
      seek_to_line_and_display(buf, buf->line);
      break;

    case 'a':
      cue ^= 1;
      break;

    case 't':
      buf->line = seek_to_line_and_display(buf,
                                            input_integer("Go to line: "));
      break;

    case 'r':
      initialize_windows();
      seek_to_line_and_display(buf, buf->line);
      break;

    case 'R':
      free(buf->filename);
      buf->filename = input_string("Enter a new file name: ");
      buf->is_file = 0;
      break;

    case 'H':
      return HELP;

    case ':': /* Buffer manipulation submenu */
      switch (input_char("n)ext, p)revious, d)elete, e)xamine, r)eload, o)pen, l)ist, or q)uit? ")) {
      case 'n':	return NEXT_FILE;
      case 'p':	return PREV_FILE;
      case 'd':	return DELETE_FILE;
      case 'e':	return LOAD_FILE;
      case 'r': return RELOAD_FILE;
      case 'o':
	find_inline_filename(buf);
	if (inline_file != NULL) return LOAD_INLINE;
	break;
      case 'l': return LOAD_LIST;
      case 'q':	return QUIT;
      }
      break;

    default:
      buf->message = "Unknown command. Try 'H' for help.";
    }

    update_status_bar(buf);
    if (!(buf->paused) && buf->jump != 0) {
      int num_jumps = abs(buf->jump);
      count += 20000;
      usleep(10000);
      if (count > (buf->delay * num_jumps)) {
	int direction = num_jumps / buf->jump, i = 0;
	count = 0;
	for (i = 0; i < num_jumps &&
	       ((buf->line < buf->lines - (LINES - 3) && direction > 0) ||
		(buf->line > 0 && direction < 0));
	     i++) {
	  buf->line = seek_to_line_and_display(buf, buf->line + direction);
	  update_status_bar(buf);
	  usleep(1); /* Smooth it, so it's easier to read */
	}
	if ((buf->jump < 0 && buf->line == 0) ||
	    (buf->jump > 0 && buf->line == buf->lines - (LINES - 3)))
	  pause_file(buf);
      }
    }
  }
  return QUIT;
}

/* Replace an existing buffer with one already open. */
void replace_buffer(Buffer *from, Buffer *to) {
  find_line_markers(to);
  to->n = from->n;
  to->p = from->p;
  to->line = from->line;
  to->delay = from->delay;
  to->jump = from->jump;
  to->paused = from->paused;
  delete_buffer(from);
  if (to->n) to->n->p = to;
  if (to->p) to->p->n = to;
}

/* Wrap load_file with some list-handling capabilities. */
Buffer *load_new_buffer(Buffer *old_buf, const char *filename) {
  Buffer *new_buf = load_file(filename), *working = NULL, *first = old_buf;

  while (first->p != NULL) first = first->p;

  /* Check to see if the file is open already and remove it if it is... */
  for (working = first; working != NULL; working = working->n)
    if (new_buf && !strcoll(new_buf->filename, working->filename)) {
      replace_buffer(working, new_buf);
      return new_buf;
    } else if (new_buf == NULL && !working->is_file &&
	       !strcoll(filename, working->filename)) {
      working->message = "Buffer is already open, going to it.";
      return working;
    }
  

  if (new_buf == NULL) {
    alert();
    old_buf->message = "There was an error loading the file.";
    return old_buf;
  } else {
    if (old_buf->n) old_buf->n->p = new_buf;
    new_buf->n = old_buf->n;
    new_buf->p = old_buf;
    old_buf->n = new_buf;
    return new_buf;
  }
}

void parse_rc_file() {
  char rcfile[strlen(getenv("HOME")) + 9];
  FILE *rc;
  sprintf(rcfile, "%s/.reedrc", getenv("HOME"));
  if ((rc = fopen(rcfile, "r")) != NULL) {
    char line[512];
    while (fgets(line, 512, rc)) {
      char key[10], value[10];
      if (sscanf(line, "%10s %10s\n", key, value) == 2) {
        if (!strcasecmp(key, "delay")) initial_delay = (atof(value) * 100000);
        else if (!strcasecmp(key, "jump")) initial_jump = atoi(value);
        else if (!strcasecmp(key, "beep") && !strcasecmp(value, "off"))
          cue = 0;
       else if (!strcasecmp(key, "paused") && !strcasecmp(value, "off"))
          initial_pause = 0;
      }
    }
    fclose(rc);
  }
}

void print_help() {
  fprintf(stderr, "Reed %s - An autoscrolling text pager\n", VERSION);
  fprintf(stderr, "Proper usage: reed [options] [filenames]\n");
  fprintf(stderr, "Options (overriding ~/.reedrc):\n");
  fprintf(stderr, " -d [num]\tScrolling delay in approximately tenths of a second.\n");
  fprintf(stderr, " -j [num]\tNumber of lines to jump when scrolling.\n");
  fprintf(stderr, " -u/-p\t\tStarted unpaused or paused, respectively.\n");
  fprintf(stderr, " -b/-q\t\tTurn audio cues on or off, respectively.\n");
  fprintf(stderr, " -v\t\tDisplay version and copyright information.\n");
  fprintf(stderr, " -h\t\tDisplay this help text.\n");
  exit(0);
}

void print_version() {
  fprintf(stderr, "Reed %s - An autoscrolling text pager\n", VERSION);
  fprintf(stderr, "Copyright (c)2000-2002 Joe Wreschnig\n\n");
  fprintf(stderr, "This program is free software; you can redistribute it and/or modify\n"); 
  fprintf(stderr, "it under the terms of the GNU General Public License as published by\n"); 
  fprintf(stderr, "the Free Software Foundation; either version 2 of the License, or\n");
  fprintf(stderr, "(at your option) any later version.\n\n");
  fprintf(stderr, "This program is distributed in the hope that it will be useful,\n");
  fprintf(stderr, "but WITHOUT ANY WARRANTY; without even the implied warranty of\n");
  fprintf(stderr, "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n");
  fprintf(stderr, "GNU General Public License for more details.\n");
  exit(0);
}

void initialize_windows(void) {
  if (status != NULL) delwin(status);
  if (text != NULL) delwin(text);
  status = newwin(2, COLS, 0, 0);
  text = newwin(LINES - 3, COLS, 3, 0);
  mvhline(2, 0, '-', COLS); refresh();
  scrollok(text, TRUE); noecho(); cbreak(); keypad(text, TRUE); curs_set(0);
  wclear(text); wclear(status);
  curs_set(0);
}

int main(int argc, char **argv) {
  char *f = NULL, opt;
  int action = 0, i = 0;
  struct stat st;
  Buffer *active = NULL;

  if (argc > 1 && !strcoll(argv[1], "--help")) print_help();

  bookmark_file = xmalloc(sizeof(char) * (strlen(getenv("HOME")) + 17));
  sprintf(bookmark_file, "%s/.reed_bookmarks", getenv("HOME"));

  parse_rc_file();

  while ((opt = getopt(argc, argv, "bqhvd:j:puf:")) != -1) {
    switch (opt) {
    case 'h': print_help();
    case 'v': print_version();
    case 'd': initial_delay = atof(optarg) * 100000; break;
    case 'j': initial_jump = atoi(optarg); break;
    case 'p': initial_pause = 1; break;
    case 'u': initial_pause = 0; break;
    case 'b': cue = 1; break;
    case 'q': cue = 0; break;
    case 'f': f = optarg;
    }
  }

  initscr();
  
  for (i = optind; i < argc; i++) {
    int valid = stat(argv[i], &st);
    if (valid == -1 && strcoll(argv[i], "-")) {
      fprintf(stderr, "reed: warning: %s: Invalid filename. Ignoring it...\n",
	      argv[i]);
    } else {
      if (active == NULL) active = load_file(argv[i]);
      else {
	Buffer *new_buf = load_file(argv[i]);
	if (new_buf != NULL) {
	  active->n = new_buf;
	  new_buf->p = active;
	  active = new_buf;
	}
      }
      if (f != NULL) {
        free(active->filename);
        active->filename = strdup(f);
        active->is_file = 0;
      }
    }
  }
  
  if (active == NULL) {
    if (isatty(fileno(stdin)) == 0) {
      active = load_file("-");
    } else {
      endwin();
      fprintf(stderr, "reed: error: No valid filenames were found.\n");
      exit(1);
    }
  }

  initialize_windows();

  while (active->p != NULL) {
    active = active->p;
  }

  waddstr(text, "Please wait... loading file.\n");
  wrefresh(text);
  find_line_markers(active);
  
  while ((action = activate_buffer(active)) != QUIT) {
    wclear(status);
    wclear(text);
    switch (action) {
    case NEXT_FILE:
      active = next_buffer(active);
      break;
    case PREV_FILE:
      active = prev_buffer(active);
      break;
    case DELETE_FILE:
      if (active->p) {
	active = active->p;
	delete_buffer(active->n);
      } else if (active->n) {
	active = active->n;
	delete_buffer(active->p);
      } else {
	delete_buffer(active);
	active = NULL;
      }
      break;
    case RELOAD_FILE:
      initialize_windows();
      if (active->is_file) {
       active = load_new_buffer(active, active->filename);
       delete_buffer(active->p);
      }
      find_line_markers(active);
      break;
    case LOAD_FILE:
      f = input_string("Enter a filename to load: ");
      active = load_new_buffer(active, f);
      free(f);
      break;
    case LOAD_INLINE:
      active = load_new_buffer(active, inline_file);
      if (inline_mark_name) {
	int i = get_bookmark(inline_mark_name, active);
	if (i >= 0) active->line = i;
	free(inline_mark_name);
	inline_mark_name = NULL;
      }
      free(inline_file);
      inline_file = NULL;
      break;
    case LOAD_LIST:
      active = load_buffer_list(active);
      break;
    case BOOKMARKS:
      active = load_bookmarks(active);
      break;
    case HELP:
      active = load_help_file(active);
      break;
    }
    if (active == NULL) break;
    else if (active->markers == NULL) {
      waddstr(text, "Please wait... loading file.\n");
      wrefresh(text);
      find_line_markers(active);
    }
  }
  endwin();
  exit(0);
}
