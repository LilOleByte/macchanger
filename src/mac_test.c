#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mac.h"

static int failures = 0;

static void
expect (int condition, const char *message)
{
	if (!condition) {
		fprintf (stderr, "FAIL: %s\n", message);
		failures++;
	}
}

static void
test_parse (void)
{
	mac_t mac;
	memset (&mac, 0, sizeof(mac));

	expect (mc_mac_read_string (&mac, "aa:bb:cc:dd:ee:ff") == 0, "parse colon form");
	expect (mac.byte[0] == 0xaa && mac.byte[5] == 0xff, "colon bytes");

	expect (mc_mac_read_string (&mac, "AA-BB-CC-DD-EE-FF") == 0, "parse dash form");
	expect (mac.byte[0] == 0xaa && mac.byte[3] == 0xdd, "dash bytes");

	expect (mc_mac_read_string (&mac, "001122334455") == 0, "parse compact form");
	expect (mac.byte[0] == 0x00 && mac.byte[1] == 0x11 && mac.byte[5] == 0x55,
		"compact bytes");

	expect (mc_mac_read_string (&mac, "00:11:22:33:44") < 0, "reject short form");
	expect (mc_mac_read_string (&mac, "00:11-22:33:44:55") < 0, "reject mixed separators");
	expect (mc_mac_read_string (&mac, "00:11:22:33:44:GG") < 0, "reject non-hex");
	expect (mc_mac_read_string (&mac, NULL) < 0, "reject missing string");
}

static void
test_random (void)
{
	mac_t mac;
	int   i;
	int   saw_ff = 0;

	srandom (1);
	memset (&mac, 0x11, sizeof(mac));
	mc_mac_random (&mac, 3, 1);
	expect (mac.byte[0] == 0x11 && mac.byte[1] == 0x11 && mac.byte[2] == 0x11,
		"ending keeps vendor bytes");
	mac.byte[0] = 0x02;
	mac.byte[1] = 0x10;
	mac.byte[2] = 0x20;
	mc_mac_random (&mac, 3, 1);
	expect (mac.byte[0] == 0x02 && mac.byte[1] == 0x10 && mac.byte[2] == 0x20,
		"ending preserves a locally administered vendor prefix");
	expect (!(mac.byte[3] == 0x11 && mac.byte[4] == 0x11 && mac.byte[5] == 0x11),
		"ending changes the last three bytes");

	for (i = 0; i < 64; i++) {
		memset (&mac, 0, sizeof(mac));
		mc_mac_random (&mac, 6, 0);
		expect ((mac.byte[0] & 0x01) == 0, "random address is unicast");
		expect ((mac.byte[0] & 0x02) != 0, "random address is locally administered");
	}

	for (i = 0; i < 64; i++) {
		memset (&mac, 0, sizeof(mac));
		mc_mac_random (&mac, 6, 1);
		expect ((mac.byte[0] & 0x01) == 0, "bia address is unicast");
		expect ((mac.byte[0] & 0x02) == 0, "bia address clears the local bit");
	}

	srandom (12345);
	for (i = 0; i < 4000 && !saw_ff; i++) {
		mc_mac_random (&mac, 6, 0);
		if (mac.byte[1] == 0xff || mac.byte[2] == 0xff || mac.byte[3] == 0xff ||
		    mac.byte[4] == 0xff || mac.byte[5] == 0xff)
			saw_ff = 1;
	}
	expect (saw_ff, "random bytes can be 0xff");
}

static void
test_equal (void)
{
	mac_t a, b;
	char  text[18];

	memset (&a, 0, sizeof(a));
	a.byte[0] = 1;
	a.byte[5] = 2;
	memcpy (&b, &a, sizeof(b));
	expect (mc_mac_equal (&a, &b), "equal addresses match");
	b.byte[5] = 3;
	expect (!mc_mac_equal (&a, &b), "different addresses do not match");

	mc_mac_into_string (&a, text);
	expect (strcmp (text, "01:00:00:00:00:02") == 0, "format address");
}

int
main (void)
{
	test_parse ();
	test_random ();
	test_equal ();
	if (failures) {
		fprintf (stderr, "%d failure(s)\n", failures);
		return 1;
	}
	return 0;
}
