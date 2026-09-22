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


#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef HAVE_SYS_RANDOM_H
# include <sys/random.h>
#endif

#include "mac.h"


mac_t *
mc_mac_dup (const mac_t *mac)
{
	mac_t *new;

	new = (mac_t *)malloc(sizeof(mac_t));
	if (new)
		memcpy (new, mac, sizeof(mac_t));
	return new;
}


void
mc_mac_free (mac_t *mac)
{
	free (mac);
}


void
mc_mac_into_string (const mac_t *mac, char *s)
{
	snprintf (s, 18, "%02x:%02x:%02x:%02x:%02x:%02x",
		  mac->byte[0], mac->byte[1], mac->byte[2],
		  mac->byte[3], mac->byte[4], mac->byte[5]);
}


static int
fill_random (unsigned char *dst, size_t len)
{
	size_t got = 0;

#if defined(HAVE_GETRANDOM) && defined(HAVE_SYS_RANDOM_H)
	if (getrandom (dst, len, 0) == (ssize_t)len)
		return 0;
#endif

	{
		int fd = open ("/dev/urandom", O_RDONLY);
		if (fd >= 0) {
			while (got < len) {
				ssize_t n = read (fd, dst + got, len - got);
				if (n <= 0)
					break;
				got += (size_t)n;
			}
			close (fd);
			if (got == len)
				return 0;
		}
	}

	for (got = 0; got < len; got++)
		dst[got] = (unsigned char)(random() & 0xFF);
	return 0;
}


unsigned
mc_random_uniform (unsigned n)
{
	unsigned char buf[4];
	uint32_t      value;
	uint32_t      limit;

	if (n <= 1)
		return 0;

	/* Reject values that would bias the modulo. */
	limit = (UINT32_MAX / (uint32_t)n) * (uint32_t)n;
	do {
		fill_random (buf, sizeof(buf));
		memcpy (&value, buf, sizeof(value));
	} while (value >= limit);

	return (unsigned)(value % (uint32_t)n);
}


void
mc_mac_random (mac_t *mac, unsigned char last_n_bytes, char set_bia)
{
	mac_t orig;
	int   attempt;
	int   start;

	/* Bit 0 of the first octet is the unicast/multicast bit and must
	 * stay clear. Bit 1 is the local/universal bit: set for a random
	 * address, clear when pretending to be a burned-in address.
	 * Randomizing only the ending must leave the vendor bytes alone.
	 */
	if (last_n_bytes != 3 && last_n_bytes != 6)
		return;

	memcpy (&orig, mac, sizeof(orig));
	start = 6 - last_n_bytes;

	for (attempt = 0; attempt < 16; attempt++) {
		fill_random (mac->byte + start, (size_t)(6 - start));

		if (last_n_bytes == 6) {
			mac->byte[0] &= 0xFC;
			if (!set_bia)
				mac->byte[0] |= 0x02;
		}

		if (!mc_mac_equal (&orig, mac))
			return;
	}
}


int
mc_mac_equal (const mac_t *mac1, const mac_t *mac2)
{
	int i;

	for (i=0; i<6; i++) {
		if (mac1->byte[i] != mac2->byte[i]) {
			return 0;
		}
	}
	return 1;
}


static int
read_hex_byte (const char *text, unsigned int *value)
{
	char tmp[3];
	char *end;

	if (!isxdigit ((unsigned char)text[0]) ||
	    !isxdigit ((unsigned char)text[1]))
		return -1;

	tmp[0] = text[0];
	tmp[1] = text[1];
	tmp[2] = '\0';
	*value = (unsigned int)strtoul (tmp, &end, 16);
	if (*end != '\0' || *value > 0xFF)
		return -1;
	return 0;
}


int
mc_mac_read_string (mac_t *mac, const char *string)
{
	unsigned int bytes[6];
	size_t       length;
	char         separator = 0;
	int          nbyte;

	if (!string) {
		fprintf (stderr, "[ERROR] Incorrect format: missing MAC address\n");
		return -1;
	}

	length = strlen (string);
	if (length == 17) {
		separator = string[2];
		if (separator != ':' && separator != '-') {
			fprintf (stderr, "[ERROR] Incorrect format: %s\n", string);
			return -1;
		}
		for (nbyte = 2; nbyte < 16; nbyte += 3) {
			if (string[nbyte] != separator) {
				fprintf (stderr, "[ERROR] Incorrect format: %s\n", string);
				return -1;
			}
		}
		for (nbyte = 0; nbyte < 6; nbyte++) {
			if (read_hex_byte (string + (nbyte * 3), &bytes[nbyte]) < 0) {
				fprintf (stderr, "[ERROR] Incorrect format: %s\n", string);
				return -1;
			}
		}
	} else if (length == 12) {
		for (nbyte = 0; nbyte < 6; nbyte++) {
			if (read_hex_byte (string + (nbyte * 2), &bytes[nbyte]) < 0) {
				fprintf (stderr, "[ERROR] Incorrect format: %s\n", string);
				return -1;
			}
		}
	} else {
		fprintf (stderr, "[ERROR] Incorrect format: MAC length should be 17 or 12. %s(%lu)\n",
			 string, (unsigned long)length);
		return -1;
	}

	for (nbyte = 0; nbyte < 6; nbyte++)
		mac->byte[nbyte] = (unsigned char)bytes[nbyte];

	return 0;
}
