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

static WDesktopEntry *parseFile(const char *path, const char *id)
{
	FILE *f = fopen(path, "r");
	WDesktopEntry *e;
	char line[4096];
	Bool in_group = False, is_application = False;

	if (!f)
		return NULL;

	e = wmalloc(sizeof(WDesktopEntry));
	e->path = wstrdup(path);
	e->id = wstrdup(id);

	while (fgets(line, sizeof(line), f)) {
		char *key, *value, *eq;

		rtrim(line);
		if (line[0] == '[') {
			in_group = (strcmp(line, "[Desktop Entry]") == 0);
			continue;
		}
		if (!in_group || line[0] == '#' || line[0] == '\0')
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
		}
	}
	fclose(f);

	if (!is_application || !e->exec || e->exec[0] == '\0') {
		freeEntry(e);
		return NULL;
	}
	if (!e->name)
		e->name = wstrdup(id);
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
