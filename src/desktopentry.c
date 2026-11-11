/* desktopentry.c - minimal freedesktop .desktop file reader
 *
 *  Window Maker window manager
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "wconfig.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "desktopentry.h"

#define DESKTOP_SUFFIX ".desktop"

static void freeAction(void *data)
{
	WDesktopAction *a = data;

	wfree(a->id);
	wfree(a->name);
	wfree(a->exec);
	wfree(a);
}

static void freeEntry(void *data)
{
	WDesktopEntry *e = data;

	wfree(e->path);
	wfree(e->id);
	wfree(e->name);
	wfree(e->exec);
	wfree(e->try_exec);
	wfree(e->only_show_in);
	wfree(e->not_show_in);
	wfree(e->startup_wm_class);
	if (e->actions)
		WMFreeArray(e->actions);
	wfree(e);
}

static void rtrim(char *s)
{
	size_t n = strlen(s);

	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1])))
		s[--n] = '\0';
}

static Bool isTrue(const char *v)
{
	return strcasecmp(v, "true") == 0 || strcmp(v, "1") == 0;
}

/* Exec= minus the %f %F %u %U %i %c %k ... field codes; %% becomes % */
static char *stripFieldCodes(const char *exec)
{
	char *out = wmalloc(strlen(exec) + 1);
	char *o = out;
	const char *p = exec;

	while (*p) {
		if (*p == '%' && p[1]) {
			if (p[1] == '%') {
				*o++ = '%';
				p += 2;
			} else {
				p += 2;
				/* also drop a space that was only separating codes */
				while (*p == ' ' && (o == out || o[-1] == ' '))
					p++;
			}
		} else {
			*o++ = *p++;
		}
	}
	*o = '\0';
	rtrim(out);
	return out;
}

/* the action with this id, creating it (in Actions= order) when listed */
static WDesktopAction *actionFor(WDesktopEntry *e, const char *id, Bool create)
{
	WDesktopAction *a;
	int i;

	if (e->actions) {
		WM_ITERATE_ARRAY(e->actions, a, i) {
			if (strcmp(a->id, id) == 0)
				return a;
		}
	}
	if (!create)
		return NULL;
	if (!e->actions)
		e->actions = WMCreateArrayWithDestructor(4, freeAction);
	a = wmalloc(sizeof(WDesktopAction));
	a->id = wstrdup(id);
	WMAddToArray(e->actions, a);
	return a;
}

/* Actions=a;b;c; declares the actions and their order */
static void declareActions(WDesktopEntry *e, const char *list)
{
	char *copy = wstrdup(list), *p, *save = NULL;

	for (p = strtok_r(copy, ";", &save); p; p = strtok_r(NULL, ";", &save)) {
		rtrim(p);
		if (p[0])
			actionFor(e, p, True);
	}
	wfree(copy);
}

static WDesktopEntry *parseFile(const char *path, const char *id)
{
	FILE *f = fopen(path, "r");
	WDesktopEntry *e;
	WDesktopAction *action = NULL;
	char line[4096];
	Bool in_main = False, is_application = False;

	if (!f)
		return NULL;

	e = wmalloc(sizeof(WDesktopEntry));
	e->path = wstrdup(path);
	e->id = wstrdup(id);

	while (fgets(line, sizeof(line), f)) {
		char *key, *value, *eq;

		rtrim(line);
		if (line[0] == '[') {
			static const char prefix[] = "[Desktop Action ";
			size_t len = strlen(line);

			in_main = (strcmp(line, "[Desktop Entry]") == 0);
			action = NULL;
			if (strncmp(line, prefix, sizeof(prefix) - 1) == 0 && line[len - 1] == ']') {
				line[len - 1] = '\0';
				/* only actions announced in Actions= count; the main
				 * group precedes the action groups in practice */
				action = actionFor(e, line + sizeof(prefix) - 1, False);
			}
			continue;
		}
		if ((!in_main && !action) || line[0] == '#' || line[0] == '\0')
			continue;

		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = line;
		value = eq + 1;
		rtrim(key);
		while (*value == ' ' || *value == '\t')
			value++;

		if (action) {
			if (strcmp(key, "Name") == 0) {
				if (!action->name)
					action->name = wstrdup(value);
			} else if (strcmp(key, "Exec") == 0) {
				if (!action->exec)
					action->exec = stripFieldCodes(value);
			}
			continue;
		}

		if (strcmp(key, "Type") == 0) {
			is_application = (strcmp(value, "Application") == 0);
		} else if (strcmp(key, "Name") == 0) {
			if (!e->name)
				e->name = wstrdup(value);
		} else if (strcmp(key, "Exec") == 0) {
			if (!e->exec)
				e->exec = stripFieldCodes(value);
		} else if (strcmp(key, "TryExec") == 0) {
			if (!e->try_exec)
				e->try_exec = wstrdup(value);
		} else if (strcmp(key, "OnlyShowIn") == 0) {
			if (!e->only_show_in)
				e->only_show_in = wstrdup(value);
		} else if (strcmp(key, "NotShowIn") == 0) {
			if (!e->not_show_in)
				e->not_show_in = wstrdup(value);
		} else if (strcmp(key, "Hidden") == 0 || strcmp(key, "NoDisplay") == 0) {
			if (isTrue(value))
				e->hidden = 1;
		} else if (strcmp(key, "Terminal") == 0) {
			e->terminal = isTrue(value);
		} else if (strcmp(key, "X-GNOME-Autostart-enabled") == 0) {
			if (!isTrue(value))
				e->autostart_disabled = 1;
		} else if (strcmp(key, "StartupWMClass") == 0) {
			if (!e->startup_wm_class && value[0])
				e->startup_wm_class = wstrdup(value);
		} else if (strcmp(key, "Actions") == 0) {
			declareActions(e, value);
		}
	}
	fclose(f);

	if (!is_application || !e->exec || e->exec[0] == '\0') {
		freeEntry(e);
		return NULL;
	}
	if (!e->name)
		e->name = wstrdup(id);

	/* drop declared actions that have no usable group */
	if (e->actions) {
		int i;

		for (i = WMGetArrayItemCount(e->actions) - 1; i >= 0; i--) {
			WDesktopAction *a = WMGetFromArray(e->actions, i);

			if (!a->exec || a->exec[0] == '\0')
				WMDeleteFromArray(e->actions, i);
			else if (!a->name)
				a->name = wstrdup(a->id);
		}
		if (WMGetArrayItemCount(e->actions) == 0) {
			WMFreeArray(e->actions);
			e->actions = NULL;
		}
	}
	return e;
}

static Bool haveId(WMArray *entries, const char *id)
{
	WDesktopEntry *e;
	int i;

	WM_ITERATE_ARRAY(entries, e, i) {
		if (strcmp(e->id, id) == 0)
			return True;
	}
	return False;
}

static int compareByName(const void *a, const void *b)
{
	const WDesktopEntry *const *ea = a, *const *eb = b;

	return strcasecmp((*ea)->name, (*eb)->name);
}

WMArray *wDesktopEntriesLoad(const char *const *dirs, int ndirs)
{
	WMArray *entries = WMCreateArrayWithDestructor(64, freeEntry);
	int d;

	for (d = 0; d < ndirs; d++) {
		DIR *dir = opendir(dirs[d]);
		struct dirent *de;

		if (!dir)
			continue;
		while ((de = readdir(dir)) != NULL) {
			size_t len = strlen(de->d_name);
			char *id, *path;
			WDesktopEntry *e;
			struct stat st;

			if (len <= strlen(DESKTOP_SUFFIX)
			    || strcmp(de->d_name + len - strlen(DESKTOP_SUFFIX), DESKTOP_SUFFIX) != 0)
				continue;
			id = wstrndup(de->d_name, len - strlen(DESKTOP_SUFFIX));
			if (haveId(entries, id)) {
				wfree(id);
				continue;
			}
			path = wstrconcat(dirs[d], "/");
			path = wstrappend(path, de->d_name);
			if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
				e = parseFile(path, id);
				if (e)
					WMAddToArray(entries, e);
			}
			wfree(path);
			wfree(id);
		}
		closedir(dir);
	}

	WMSortArray(entries, compareByName);
	return entries;
}

void wDesktopEntriesFree(WMArray *entries)
{
	if (entries)
		WMFreeArray(entries);
}

/* ---------------------------------------------------------------------- */
/* the installed applications                                             */

static const char *const *applicationDirs(int *count)
{
	static char *dirs[32];
	static int ndirs = -1;

	if (ndirs < 0) {
		const char *home = getenv("HOME");
		const char *data_home = getenv("XDG_DATA_HOME");
		const char *data_dirs = getenv("XDG_DATA_DIRS");
		char *list, *p, *save = NULL;

		ndirs = 0;
		if (data_home && data_home[0])
			dirs[ndirs++] = wstrconcat(data_home, "/applications");
		else if (home && home[0])
			dirs[ndirs++] = wstrconcat(home, "/.local/share/applications");
		list = wstrdup((data_dirs && data_dirs[0]) ? data_dirs : "/usr/local/share:/usr/share");
		for (p = strtok_r(list, ":", &save); p && ndirs < 31; p = strtok_r(NULL, ":", &save))
			if (p[0])
				dirs[ndirs++] = wstrconcat(p, "/applications");
		wfree(list);
	}
	*count = ndirs;
	return (const char *const *)dirs;
}

/* a cheap signature of the directories: the sum of their mtimes */
static long applicationDirsStamp(const char *const *dirs, int ndirs)
{
	long stamp = 0;
	int d;

	for (d = 0; d < ndirs; d++) {
		struct stat st;

		if (stat(dirs[d], &st) == 0)
			stamp += (long)st.st_mtime + d;
	}
	return stamp;
}

WMArray *wDesktopEntriesApplications(void)
{
	static WMArray *apps = NULL;
	static long stamp = -1;
	const char *const *dirs;
	int ndirs = 0;
	long now;

	dirs = applicationDirs(&ndirs);
	now = applicationDirsStamp(dirs, ndirs);
	if (!apps || now != stamp) {
		if (apps)
			WMFreeArray(apps);
		apps = wDesktopEntriesLoad(dirs, ndirs);
		stamp = now;
	}
	return apps;
}

/* basename of the first word of a command line, in a static buffer;
 * an "env VAR=value ..." prefix and surrounding quotes are skipped */
static const char *programOf(const char *command)
{
	static char buf[256];
	const char *p = command, *end, *q;
	Bool after_env = False;
	size_t len;

	if (!command)
		return NULL;
	for (;;) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0')
			return NULL;
		end = p;
		while (*end && *end != ' ' && *end != '\t')
			end++;
		if (!after_env && end - p == 3 && strncmp(p, "env", 3) == 0) {
			after_env = True;
			p = end;
			continue;
		}
		if (after_env && memchr(p, '=', end - p)) {
			p = end;
			continue;
		}
		break;
	}
	if (*p == '"' || *p == '\'') {
		p++;
		if (end > p && (end[-1] == '"' || end[-1] == '\''))
			end--;
	}
	for (q = end; q > p; q--) {
		if (q[-1] == '/') {
			p = q;
			break;
		}
	}
	len = end - p;
	if (len == 0 || len >= sizeof(buf))
		return NULL;
	memcpy(buf, p, len);
	buf[len] = '\0';
	return buf;
}

/* "org.kde.konsole" ends in ".konsole"; plain ids compare whole */
static Bool idMatchesName(const char *id, const char *name)
{
	size_t il = strlen(id), nl = strlen(name);

	if (strcasecmp(id, name) == 0)
		return True;
	return il > nl + 1 && id[il - nl - 1] == '.' && strcasecmp(id + il - nl, name) == 0;
}

const WDesktopEntry *wDesktopEntryForApp(const char *desktop_id, const char *wm_instance,
					 const char *wm_class, const char *command)
{
	WMArray *apps = wDesktopEntriesApplications();
	WDesktopEntry *e;
	const char *prog;
	char *prog_copy = NULL;
	int i;

	if (!apps)
		return NULL;

	if (desktop_id && desktop_id[0]) {
		size_t len = strlen(desktop_id);
		size_t suffix = strlen(DESKTOP_SUFFIX);

		if (len > suffix && strcmp(desktop_id + len - suffix, DESKTOP_SUFFIX) == 0)
			len -= suffix;
		WM_ITERATE_ARRAY(apps, e, i) {
			if (strlen(e->id) == len && strncmp(e->id, desktop_id, len) == 0)
				return e;
		}
	}
	if (wm_class && wm_class[0]) {
		WM_ITERATE_ARRAY(apps, e, i) {
			if (e->startup_wm_class && strcasecmp(e->startup_wm_class, wm_class) == 0)
				return e;
		}
		WM_ITERATE_ARRAY(apps, e, i) {
			if (idMatchesName(e->id, wm_class))
				return e;
		}
	}
	if (wm_instance && wm_instance[0]) {
		WM_ITERATE_ARRAY(apps, e, i) {
			if (e->startup_wm_class && strcasecmp(e->startup_wm_class, wm_instance) == 0)
				return e;
		}
		WM_ITERATE_ARRAY(apps, e, i) {
			if (idMatchesName(e->id, wm_instance))
				return e;
		}
	}
	prog = programOf(command);
	if (prog) {
		prog_copy = wstrdup(prog);
		WM_ITERATE_ARRAY(apps, e, i) {
			const char *ep = programOf(e->exec);

			if (ep && strcmp(ep, prog_copy) == 0) {
				wfree(prog_copy);
				return e;
			}
		}
		wfree(prog_copy);
	}
	return NULL;
}

char *wDesktopEntryCommand(const WDesktopEntry *entry)
{
	if (entry->terminal)
		return wstrconcat("x-terminal-emulator -e ", entry->exec);
	return wstrdup(entry->exec);
}

static Bool listHas(const char *list, const char *item)
{
	const char *p = list;
	size_t n = strlen(item);

	while (p && *p) {
		const char *end = strchr(p, ';');
		size_t len = end ? (size_t)(end - p) : strlen(p);

		if (len == n && strncasecmp(p, item, n) == 0)
			return True;
		p = end ? end + 1 : NULL;
	}
	return False;
}

Bool wDesktopEntryShowsIn(const WDesktopEntry *entry, const char *desktop)
{
	if (entry->only_show_in && entry->only_show_in[0])
		return listHas(entry->only_show_in, desktop);
	if (entry->not_show_in && entry->not_show_in[0])
		return !listHas(entry->not_show_in, desktop);
	return True;
}

Bool wDesktopEntryTryExecOk(const WDesktopEntry *entry)
{
	const char *path_env;
	char *paths, *p, *save = NULL;
	Bool found = False;

	if (!entry->try_exec || entry->try_exec[0] == '\0')
		return True;
	if (entry->try_exec[0] == '/')
		return access(entry->try_exec, X_OK) == 0;

	path_env = getenv("PATH");
	if (!path_env)
		path_env = "/usr/local/bin:/usr/bin:/bin";
	paths = wstrdup(path_env);
	for (p = strtok_r(paths, ":", &save); p && !found; p = strtok_r(NULL, ":", &save)) {
		char *full = wstrconcat(p, "/");

		full = wstrappend(full, entry->try_exec);
		found = (access(full, X_OK) == 0);
		wfree(full);
	}
	wfree(paths);
	return found;
}
