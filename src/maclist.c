/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* MAC Changer
 *
 * Authors:
 *      Alvaro Lopez Ortega <alvaro@alobbs.com>
 *
 * Copyright (C) 2002,2013 Alvaro Lopez Ortega
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "maclist.h"

typedef struct {
	unsigned char byte[3];
	unsigned char wireless;
	unsigned int  name_off;
} oui_item_t;

static oui_item_t *items = NULL;
static int         item_count = 0;
static int         item_cap = 0;

static char       *names = NULL;
static size_t      names_len = 0;
static size_t      names_cap = 0;

static int        *wireless_index = NULL;
static int         wireless_index_len = 0;
static int        *other_index = NULL;
static int         other_index_len = 0;


int
mc_maclist_keyword_matches (const char *name, const char *keyword)
{
	size_t nlen;
	const char *p;

	if (!keyword || keyword[0] == '\0')
		return 1;
	if (!name)
		return 0;

	nlen = strlen (keyword);
	for (p = name; *p; p++) {
		if (strncasecmp (p, keyword, nlen) != 0)
			continue;
		if (p != name && isalnum ((unsigned char)p[-1]))
			continue;
		if (isalnum ((unsigned char)p[nlen]))
			continue;
		return 1;
	}
	return 0;
}


static int
name_looks_wireless (const char *name)
{
	static const char *const keys[] = {
		"wireless", "wi-fi", "wifi", "wlan", "802.11", NULL
	};
	int i;

	for (i = 0; keys[i]; i++) {
		if (mc_maclist_keyword_matches (name, keys[i]))
			return 1;
	}
	return 0;
}


static int
store_name (const char *name, unsigned int *off)
{
	size_t n = strlen (name) + 1;
	char *grown;
	size_t cap;

	if (names_len + n > names_cap) {
		cap = names_cap ? names_cap * 2 : 8192;
		while (cap < names_len + n)
			cap *= 2;
		grown = (char *)realloc (names, cap);
		if (!grown)
			return -1;
		names = grown;
		names_cap = cap;
	}

	memcpy (names + names_len, name, n);
	*off = (unsigned int)names_len;
	names_len += n;
	return 0;
}


static int
append_item (const unsigned char byte[3], const char *name, int wireless)
{
	oui_item_t *grown;

	if (item_count == item_cap) {
		int cap = item_cap ? item_cap * 2 : 256;
		grown = (oui_item_t *)realloc (items, (size_t)cap * sizeof(oui_item_t));
		if (!grown)
			return -1;
		items = grown;
		item_cap = cap;
	}

	memcpy (items[item_count].byte, byte, 3);
	items[item_count].wireless = wireless ? 1 : 0;
	if (store_name (name, &items[item_count].name_off) < 0)
		return -1;
	item_count++;
	return 0;
}


static int
parse_oui_line (char *line, int from_wireless_list)
{
	unsigned int b0, b1, b2;
	int          name_at = 0;
	char        *name;
	size_t       len;
	unsigned char byte[3];
	int          wireless;

	if (line[0] == '\0' || line[0] == '#')
		return 0;

	if (sscanf (line, "%2x %2x %2x %n", &b0, &b1, &b2, &name_at) != 3 || name_at <= 0)
		return 0;
	if (b0 > 0xFF || b1 > 0xFF || b2 > 0xFF)
		return 0;

	name = line + name_at;
	while (*name == ' ' || *name == '\t')
		name++;

	len = strlen (name);
	while (len > 0 && (name[len - 1] == '\n' || name[len - 1] == '\r' ||
			   name[len - 1] == ' ' || name[len - 1] == '\t')) {
		name[--len] = '\0';
	}
	if (len == 0)
		return 0;

	byte[0] = (unsigned char)b0;
	byte[1] = (unsigned char)b1;
	byte[2] = (unsigned char)b2;
	wireless = from_wireless_list || name_looks_wireless (name);
	return append_item (byte, name, wireless);
}


static int
read_file (const char *fullpath, int from_wireless_list)
{
	FILE *f;
	char  tmp[1024];

	if ((f = fopen (fullpath, "r")) == NULL) {
		fprintf (stderr, "[ERROR] Could not read data file: %s\n", fullpath);
		return -1;
	}

	while (fgets (tmp, sizeof(tmp), f) != NULL) {
		if (strchr (tmp, '\n') == NULL && !feof (f)) {
			int c;
			while ((c = fgetc (f)) != EOF && c != '\n')
				;
		}
		if (parse_oui_line (tmp, from_wireless_list) < 0) {
			fclose (f);
			return -1;
		}
	}

	fclose (f);
	return 0;
}


static int
cmp_item (const void *a, const void *b)
{
	const oui_item_t *ia = (const oui_item_t *)a;
	const oui_item_t *ib = (const oui_item_t *)b;
	return memcmp (ia->byte, ib->byte, 3);
}


static int
cmp_key (const void *key, const void *elem)
{
	const oui_item_t *item = (const oui_item_t *)elem;
	return memcmp (key, item->byte, 3);
}


static void
dedupe_items (void)
{
	int r, w = 0;

	for (r = 0; r < item_count; r++) {
		if (w > 0 && memcmp (items[w - 1].byte, items[r].byte, 3) == 0) {
			if (items[r].wireless)
				items[w - 1].wireless = 1;
			continue;
		}
		items[w++] = items[r];
	}
	item_count = w;
}


static int
build_indexes (void)
{
	int i;

	wireless_index = (int *)malloc ((size_t)item_count * sizeof(int));
	other_index = (int *)malloc ((size_t)item_count * sizeof(int));
	if (!wireless_index || !other_index)
		return -1;

	for (i = 0; i < item_count; i++) {
		if (items[i].wireless)
			wireless_index[wireless_index_len++] = i;
		else
			other_index[other_index_len++] = i;
	}
	return 0;
}


static const oui_item_t *
lookup_oui (const mac_t *mac)
{
	if (!items || item_count <= 0)
		return NULL;
	return (const oui_item_t *)bsearch (mac->byte, items, (size_t)item_count,
					    sizeof(oui_item_t), cmp_key);
}


static const char *
item_name (const oui_item_t *item)
{
	if (!item || !names)
		return NULL;
	return names + item->name_off;
}


const char *
mc_maclist_get_cardname_with_default (const mac_t *mac, const char *def)
{
	const oui_item_t *item = lookup_oui (mac);
	const char *name = item_name (item);
	return name ? name : def;
}


static void
set_vendor_from_index (mac_t *mac, const int *index, int len)
{
	int id;

	if (!index || len <= 0)
		return;
	id = index[mc_random_uniform ((unsigned)len)];
	memcpy (mac->byte, items[id].byte, 3);
}


void
mc_maclist_set_random_vendor (mac_t *mac, mac_type_t type)
{
	unsigned char byte[3];
	int id;

	if (item_count <= 0)
		return;

	switch (type) {
	case mac_is_wireless:
		set_vendor_from_index (mac, wireless_index, wireless_index_len);
		break;
	case mac_is_others:
		set_vendor_from_index (mac, other_index, other_index_len);
		break;
	case mac_is_anykind:
		id = (int)mc_random_uniform ((unsigned)item_count);
		memcpy (byte, items[id].byte, 3);
		memcpy (mac->byte, byte, 3);
		break;
	}
}


int
mc_maclist_is_wireless (const mac_t *mac)
{
	const oui_item_t *item = lookup_oui (mac);
	return item && item->wireless;
}


static void
print_index (const int *index, int len, const char *keyword)
{
	int i;

	if (!index)
		return;

	for (i = 0; i < len; i++) {
		const oui_item_t *item = &items[index[i]];
		const char *name = item_name (item);
		if (!mc_maclist_keyword_matches (name, keyword))
			continue;
		printf ("%5d - %02x:%02x:%02x - %s\n", i,
			item->byte[0], item->byte[1], item->byte[2],
			name);
	}
}


void
mc_maclist_print (const char *keyword)
{
	printf ("Misc MACs:\n"
		"Num     MAC        Vendor\n"
		"---     ---        ------\n");
	print_index (other_index, other_index_len, keyword);

	printf ("\n"
		"Wireless MACs:\n"
		"Num     MAC        Vendor\n"
		"---     ---        ------\n");
	print_index (wireless_index, wireless_index_len, keyword);
}


static int
join_path (char *dst, size_t dstlen, const char *dir, const char *file)
{
	int n = snprintf (dst, dstlen, "%s/%s", dir, file);
	return (n < 0 || (size_t)n >= dstlen) ? -1 : 0;
}


static int
path_readable (const char *path)
{
	FILE *f = fopen (path, "r");
	if (!f)
		return 0;
	fclose (f);
	return 1;
}


static int
dir_has_lists (const char *dir)
{
	char path[PATH_MAX];

	if (join_path (path, sizeof(path), dir, "OUI.list") < 0 || !path_readable (path))
		return 0;
	if (join_path (path, sizeof(path), dir, "wireless.list") < 0 || !path_readable (path))
		return 0;
	return 1;
}


static int
data_dir_near_exe (char *dst, size_t dstlen)
{
	char    exe[PATH_MAX];
	ssize_t len;
	char   *slash;

	len = readlink ("/proc/self/exe", exe, sizeof(exe) - 1);
	if (len < 0)
		return -1;
	exe[len] = '\0';
	slash = strrchr (exe, '/');
	if (!slash)
		return -1;
	*slash = '\0';
	if (snprintf (dst, dstlen, "%s/../data", exe) >= (int)dstlen)
		return -1;
	return 0;
}


void
mc_maclist_free (void)
{
	free (items);
	free (names);
	free (wireless_index);
	free (other_index);
	items = NULL;
	names = NULL;
	wireless_index = NULL;
	other_index = NULL;
	item_count = item_cap = 0;
	names_len = names_cap = 0;
	wireless_index_len = other_index_len = 0;
}


int
mc_maclist_init (void)
{
	const char *candidates[4];
	char        near_exe[PATH_MAX];
	char        oui_path[PATH_MAX];
	char        wireless_path[PATH_MAX];
	int         count = 0;
	int         i;

	mc_maclist_free ();

	candidates[count++] = LISTDIR;
	if (data_dir_near_exe (near_exe, sizeof(near_exe)) == 0)
		candidates[count++] = near_exe;
	candidates[count++] = "data";
	candidates[count++] = "../data";

	for (i = 0; i < count; i++) {
		if (!dir_has_lists (candidates[i]))
			continue;
		if (join_path (oui_path, sizeof(oui_path), candidates[i], "OUI.list") < 0 ||
		    join_path (wireless_path, sizeof(wireless_path), candidates[i], "wireless.list") < 0)
			continue;

		if (read_file (oui_path, 0) == 0 &&
		    read_file (wireless_path, 1) == 0 &&
		    item_count > 0) {
			qsort (items, (size_t)item_count, sizeof(oui_item_t), cmp_item);
			dedupe_items ();
			if (build_indexes () == 0)
				return 0;
		}

		mc_maclist_free ();
	}

	fprintf (stderr, "[ERROR] Could not read MAC vendor lists (looked in %s)\n", LISTDIR);
	return -1;
}
