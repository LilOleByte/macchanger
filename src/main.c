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

#include <sys/types.h>
#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mac.h"
#include "maclist.h"
#include "netinfo.h"

#define EXIT_OK    0
#define EXIT_ERROR 1

static void
print_help (void)
{
	printf ("GNU MAC Changer\n"
		"Usage: macchanger [options] device\n\n"
		"  -h,  --help                   Print this help\n"
		"  -V,  --version                Print version and exit\n"
		"  -s,  --show                   Print the MAC address and exit\n"
		"  -e,  --ending                 Don't change the vendor bytes\n"
		"  -a,  --another                Set random vendor MAC of the same kind\n"
		"  -A                            Set random vendor MAC of any kind\n"
		"  -p,  --permanent              Reset to original, permanent hardware MAC\n"
		"  -r,  --random                 Set fully random MAC\n"
		"  -l,  --list[=keyword]         Print known vendors\n"
		"  -b,  --bia                    Pretend to be a burned-in-address\n"
		"  -m,  --mac=XX:XX:XX:XX:XX:XX\n"
		"       --mac XX:XX:XX:XX:XX:XX  Set the MAC XX:XX:XX:XX:XX:XX\n\n"
		"Report bugs to https://github.com/alobbs/macchanger/issues\n"
		"Updates: byte@jvmlab.org\n");
}


static void
print_usage (void)
{
	printf ("GNU MAC Changer\n"
		"Usage: macchanger [options] device\n\n"
		"Try `macchanger --help' for more options.\n");
}


static void
print_mac (const char *s, const mac_t *mac, int iface_wireless)
{
	char string[18];
	int  is_wireless;

	is_wireless = iface_wireless || mc_maclist_is_wireless(mac);
	mc_mac_into_string (mac, string);
	printf ("%s%s%s (%s)\n", s,
		string,
		is_wireless ? " [wireless]": "",
		CARD_NAME(mac));
	fflush (stdout);
}


static int
change_mode_count (char random, char ending, char another_any, char another_same,
		   char permanent, const char *set_mac)
{
	return (random ? 1 : 0) + (ending ? 1 : 0) + (another_any ? 1 : 0) +
	       (another_same ? 1 : 0) + (permanent ? 1 : 0) + (set_mac ? 1 : 0);
}


int
main (int argc, char *argv[])
{
	char random       = 0;
	char ending       = 0;
	char another_any  = 0;
	char another_same = 0;
	char permanent    = 0;
	char print_list   = 0;
	char show         = 0;
	char set_bia      = 0;
	char explicit_same_ok = 0;
	char *set_mac     = NULL;
	char *search_word = NULL;

	struct option long_options[] = {
		{"help",        no_argument,       NULL, 'h'},
		{"version",     no_argument,       NULL, 'V'},
		{"random",      no_argument,       NULL, 'r'},
		{"ending",      no_argument,       NULL, 'e'},
		{"endding",     no_argument,       NULL, 'e'}, /* kept for backwards compatibility */
		{"another",     no_argument,       NULL, 'a'},
		{"permanent",   no_argument,       NULL, 'p'},
		{"show",        no_argument,       NULL, 's'},
		{"another_any", no_argument,       NULL, 'A'},
		{"bia",         no_argument,       NULL, 'b'},
		{"list",        optional_argument, NULL, 'l'},
		{"mac",         required_argument, NULL, 'm'},
		{NULL, 0, NULL, 0}
	};

	net_info_t *net = NULL;
	mac_t      *mac = NULL;
	mac_t      *mac_permanent = NULL;
	mac_t      *mac_faked = NULL;
	mac_t      *mac_now = NULL;
	char       *device_name;
	int         val;
	int         ret = EXIT_ERROR;
	int         modes;
	int         iface_wireless = 0;

	while ((val = getopt_long (argc, argv, "VasAbrephl::m:", long_options, NULL)) != -1) {
		switch (val) {
		case 'V':
			printf ("GNU MAC changer %s\n"
				"Written by Alvaro Lopez Ortega <alvaro@gnu.org>\n\n"
				"Copyright (C) 2003-2026 Alvaro Lopez Ortega <alvaro@gnu.org>.\n"
				"This is free software; see the source for copying conditions.  There is NO\n"
				"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
				"Updates: byte@jvmlab.org\n",
				VERSION);
			return EXIT_OK;
		case 'l':
			print_list = 1;
			if (optarg)
				search_word = optarg;
			else if (optind < argc && argv[optind][0] != '-')
				search_word = argv[optind++];
			break;
		case 'r':
			random = 1;
			break;
		case 'e':
			ending = 1;
			break;
		case 'b':
			set_bia = 1;
			break;
		case 'a':
			another_same = 1;
			break;
		case 's':
			show = 1;
			break;
		case 'A':
			another_any = 1;
			break;
		case 'p':
			permanent = 1;
			break;
		case 'm':
			set_mac = optarg;
			break;
		case 'h':
			print_help();
			return EXIT_OK;
		case '?':
		default:
			print_help();
			return EXIT_ERROR;
		}
	}

	if (mc_maclist_init() < 0)
		return EXIT_ERROR;

	if (print_list) {
		mc_maclist_print(search_word);
		ret = EXIT_OK;
		goto cleanup;
	}

	if (optind >= argc) {
		print_usage();
		goto cleanup;
	}
	device_name = argv[optind];

	modes = change_mode_count (random, ending, another_any, another_same, permanent, set_mac);
	iface_wireless = mc_net_info_is_wireless (device_name);

	if ((net = mc_net_info_new(device_name)) == NULL)
		goto cleanup;
	mac = mc_net_info_get_mac(net);
	mac_permanent = mc_net_info_get_permanent_mac(net);
	if (!mac) {
		fprintf (stderr, "[ERROR] Out of memory\n");
		goto cleanup;
	}

	if (set_bia && !random)
		fprintf (stderr, "[WARNING] Ignoring --bia option that can only be used with --random\n");

	print_mac ("Current MAC:   ", mac, iface_wireless);
	if (mac_permanent)
		print_mac ("Permanent MAC: ", mac_permanent, iface_wireless);
	else {
		printf ("Permanent MAC: unavailable\n");
		fflush (stdout);
	}

	if (show || modes == 0) {
		ret = EXIT_OK;
		goto cleanup;
	}

	if (modes > 1) {
		fprintf (stderr, "[WARNING] Multiple change options given; only one is applied\n");
	}

	mac_faked = mc_mac_dup (mac);
	if (!mac_faked) {
		fprintf (stderr, "[ERROR] Out of memory\n");
		goto cleanup;
	}

	if (set_mac) {
		if (mc_mac_read_string (mac_faked, set_mac) < 0)
			goto cleanup;
		if (mac_faked->byte[0] & 0x01) {
			fprintf (stderr, "[ERROR] Refusing a multicast MAC address\n");
			goto cleanup;
		}
		explicit_same_ok = 1;
	} else if (random) {
		mc_mac_random (mac_faked, 6, set_bia);
	} else if (ending) {
		mc_mac_random (mac_faked, 3, 1);
	} else if (another_same) {
		mc_maclist_set_random_vendor (mac_faked,
			(iface_wireless || mc_maclist_is_wireless (mac)) ? mac_is_wireless : mac_is_others);
		mc_mac_random (mac_faked, 3, 1);
	} else if (another_any) {
		mc_maclist_set_random_vendor(mac_faked, mac_is_anykind);
		mc_mac_random (mac_faked, 3, 1);
	} else if (permanent) {
		if (!mac_permanent) {
			fprintf (stderr, "[ERROR] Refusing to change the MAC because the permanent address is unavailable\n");
			goto cleanup;
		}
		memcpy (mac_faked, mac_permanent, sizeof(*mac_faked));
		explicit_same_ok = 1;
	}

	if (mc_mac_equal (mac, mac_faked)) {
		printf ("It's the same MAC!!\n");
		fflush (stdout);
		ret = explicit_same_ok ? EXIT_OK : EXIT_ERROR;
		goto cleanup;
	}

	if (mc_net_info_set_mac (net, mac_faked) < 0)
		goto cleanup;

	mac_now = mc_net_info_get_mac(net);
	if (!mac_now) {
		fprintf (stderr, "[ERROR] Out of memory\n");
		goto cleanup;
	}
	print_mac ("New MAC:       ", mac_now, iface_wireless);
	ret = EXIT_OK;

cleanup:
	mc_mac_free (mac);
	mc_mac_free (mac_faked);
	mc_mac_free (mac_permanent);
	mc_mac_free (mac_now);
	mc_net_info_free (net);
	mc_maclist_free();
	return ret;
}
