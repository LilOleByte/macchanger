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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <linux/ethtool.h>
#include <linux/sockios.h>

#include "netinfo.h"


static struct ifreq
ifreq_named (const net_info_t *net)
{
	struct ifreq req;

	memset (&req, 0, sizeof(req));
	memcpy (req.ifr_name, net->dev.ifr_name, IFNAMSIZ);
	return req;
}


static int
mc_net_info_get_flags (const net_info_t *net, short *flags)
{
	struct ifreq req = ifreq_named (net);

	if (ioctl (net->sock, SIOCGIFFLAGS, &req) < 0)
		return -1;

	*flags = req.ifr_flags;
	return 0;
}


static int
mc_net_info_set_flags (const net_info_t *net, short flags)
{
	struct ifreq req = ifreq_named (net);

	req.ifr_flags = flags;
	return ioctl (net->sock, SIOCSIFFLAGS, &req);
}


static int
mc_net_info_reload (net_info_t *net)
{
	return ioctl (net->sock, SIOCGIFHWADDR, &net->dev);
}


static void
restore_flags (const net_info_t *net, short flags)
{
	if (mc_net_info_set_flags (net, flags) < 0) {
		fprintf (stderr, "[WARNING] Could not restore %s to its previous state: %s\n",
			 net->dev.ifr_name, strerror (errno));
	}
}


static void
print_set_error (int err)
{
	switch (err) {
	case EPERM:
	case EACCES:
		fprintf (stderr, "[ERROR] Could not change MAC: insufficient permissions (%s). Run as root.\n",
			 strerror (err));
		break;
	case EBUSY:
		fprintf (stderr, "[ERROR] Could not change MAC: device or resource busy (%s).\n",
			 strerror (err));
		break;
	case EOPNOTSUPP:
		fprintf (stderr, "[ERROR] Could not change MAC: this driver does not support changing the hardware address (%s).\n",
			 strerror (err));
		break;
	case EINVAL:
		fprintf (stderr, "[ERROR] Could not change MAC: the address was rejected (%s).\n",
			 strerror (err));
		break;
	case ENODEV:
		fprintf (stderr, "[ERROR] Could not change MAC: no such device (%s).\n",
			 strerror (err));
		break;
	default:
		fprintf (stderr, "[ERROR] Could not change MAC: %s\n", strerror (err));
		break;
	}
}


net_info_t *
mc_net_info_new (const char *device)
{
	net_info_t *new;

	if (!device || device[0] == '\0' || strlen (device) >= IFNAMSIZ) {
		fprintf (stderr, "[ERROR] Invalid interface name%s%s\n",
			 device ? ": " : "",
			 device ? device : "");
		return NULL;
	}

	new = (net_info_t *)malloc (sizeof(net_info_t));
	if (!new) {
		fprintf (stderr, "[ERROR] Out of memory\n");
		return NULL;
	}
	memset (new, 0, sizeof(*new));

	new->sock = socket (AF_INET, SOCK_DGRAM, 0);
	if (new->sock < 0) {
		perror ("[ERROR] Socket");
		free (new);
		return NULL;
	}

	memcpy (new->dev.ifr_name, device, strlen (device) + 1);
	if (mc_net_info_reload (new) < 0) {
		if (errno == ENODEV)
			fprintf (stderr, "[ERROR] No such device: %s\n", device);
		else
			fprintf (stderr, "[ERROR] Could not read MAC address for %s: %s\n",
				 device, strerror (errno));
		close (new->sock);
		free (new);
		return NULL;
	}

	return new;
}


void
mc_net_info_free (net_info_t *net)
{
	if (!net)
		return;
	close (net->sock);
	free (net);
}


mac_t *
mc_net_info_get_mac (const net_info_t *net)
{
	int    i;
	mac_t *new = (mac_t *)malloc (sizeof(mac_t));

	if (!new)
		return NULL;

	for (i=0; i<6; i++) {
		new->byte[i] = net->dev.ifr_hwaddr.sa_data[i] & 0xFF;
	}

	return new;
}


static void
fill_hwaddr_request (struct ifreq *req, const net_info_t *net, const mac_t *mac)
{
	*req = ifreq_named (net);
	req->ifr_hwaddr = net->dev.ifr_hwaddr;
	memcpy (req->ifr_hwaddr.sa_data, mac->byte, 6);
}


int
mc_net_info_set_mac (net_info_t *net, const mac_t *mac)
{
	struct ifreq req;
	short        flags = 0;
	int          was_up = 0;
	int          i;
	int          saved;

	fill_hwaddr_request (&req, net, mac);
	if (ioctl (net->sock, SIOCSIFHWADDR, &req) < 0) {
		saved = errno;
		/* Drivers such as mac80211 reject the change while the
		 * interface is running. Bring it down and try once more. */
		if (saved == EBUSY &&
		    mc_net_info_get_flags (net, &flags) == 0 &&
		    (flags & IFF_UP)) {
			fprintf (stderr, "[INFO] Device %s is busy; bringing it down to change the MAC address\n",
				 net->dev.ifr_name);
			if (mc_net_info_set_flags (net, flags & ~IFF_UP) < 0) {
				fprintf (stderr, "[ERROR] Could not bring %s down: %s\n",
					 net->dev.ifr_name, strerror (errno));
				return -1;
			}
			was_up = 1;
			fill_hwaddr_request (&req, net, mac);
			if (ioctl (net->sock, SIOCSIFHWADDR, &req) < 0) {
				saved = errno;
				restore_flags (net, flags);
				print_set_error (saved);
				return -1;
			}
		} else {
			print_set_error (saved);
			return -1;
		}
	}

	if (was_up)
		restore_flags (net, flags);

	if (mc_net_info_reload (net) < 0) {
		fprintf (stderr, "[ERROR] Could not read MAC address after changing it: %s\n",
			 strerror (errno));
		return -1;
	}

	for (i=0; i<6; i++) {
		if ((unsigned char)(net->dev.ifr_hwaddr.sa_data[i] & 0xFF) != mac->byte[i]) {
			printf ("Network driver didn't actually change to the new MAC!!\n");
			fflush (stdout);
			fprintf (stderr, "[ERROR] The interface is still using a different address. "
				 "The driver may have ignored the request, or another program such as NetworkManager may have reset it.\n");
			return -1;
		}
	}

	return 0;
}


mac_t *
mc_net_info_get_permanent_mac (const net_info_t *net)
{
	int                       i;
	struct ifreq              req;
	struct ethtool_perm_addr *epa;
	mac_t                    *newmac;

	newmac = (mac_t *)calloc (1, sizeof(mac_t));
	epa = (struct ethtool_perm_addr *)calloc (1, sizeof(struct ethtool_perm_addr) + IFHWADDRLEN);
	if (!newmac || !epa) {
		free (newmac);
		free (epa);
		fprintf (stderr, "[ERROR] Out of memory\n");
		return NULL;
	}

	epa->cmd = ETHTOOL_GPERMADDR;
	epa->size = IFHWADDRLEN;

	req = ifreq_named (net);
	req.ifr_data = (caddr_t)epa;

	if (ioctl (net->sock, SIOCETHTOOL, &req) < 0) {
		fprintf (stderr, "[WARNING] Could not read permanent MAC: %s\n", strerror (errno));
		free (epa);
		free (newmac);
		return NULL;
	}

	if (epa->size > 0) {
		int n = epa->size;
		if (n > 6)
			n = 6;
		for (i = 0; i < n; i++)
			newmac->byte[i] = epa->data[i];
	}

	free (epa);
	return newmac;
}


int
mc_net_info_is_wireless (const char *device)
{
	char path[PATH_MAX];
	int  n;

	if (!device || device[0] == '\0' || strchr (device, '/'))
		return 0;

	n = snprintf (path, sizeof(path), "/sys/class/net/%s/phy80211", device);
	if (n > 0 && (size_t)n < sizeof(path) && access (path, F_OK) == 0)
		return 1;

	n = snprintf (path, sizeof(path), "/sys/class/net/%s/wireless", device);
	if (n > 0 && (size_t)n < sizeof(path) && access (path, F_OK) == 0)
		return 1;

	return 0;
}
