#include <stdio.h>
#include <string.h>

#include "mac.h"
#include "maclist.h"

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
test_keywords (void)
{
	expect (mc_maclist_keyword_matches ("Intel Corporate", "Intel"),
		"Intel matches Intel Corporate");
	expect (!mc_maclist_keyword_matches ("Apollo Intelligent Connectivity", "Intel"),
		"Intel does not match Intelligent");
	expect (mc_maclist_keyword_matches ("TP-LINK TECHNOLOGIES CO., LTD.", "link"),
		"link matches TP-LINK");
	expect (mc_maclist_keyword_matches ("Anything", NULL),
		"missing keyword matches everything");
}

static void
test_vendors (void)
{
	mac_t mac;
	int   i;

	memset (&mac, 0, sizeof(mac));
	mac.byte[0] = 0x74;
	mac.byte[1] = 0x4c;
	mac.byte[2] = 0xa1;
	expect (!mc_maclist_is_wireless (&mac),
		"Liteon OUI is not classified as wireless by name");
	expect (strcmp (CARD_NAME (&mac), "Liteon Technology Corporation") == 0,
		"Liteon vendor name");

	mac.byte[0] = 0x00;
	mac.byte[1] = 0x00;
	mac.byte[2] = 0x8f;
	expect (mc_maclist_is_wireless (&mac),
		"wireless.list entry stays wireless");

	for (i = 0; i < 40; i++) {
		mc_maclist_set_random_vendor (&mac, mac_is_wireless);
		expect (mc_maclist_is_wireless (&mac),
			"wireless vendor pool stays wireless");
	}

	for (i = 0; i < 40; i++) {
		mc_maclist_set_random_vendor (&mac, mac_is_others);
		expect (!mc_maclist_is_wireless (&mac),
			"other vendor pool stays non-wireless");
	}
}

int
main (void)
{
	test_keywords ();
	if (mc_maclist_init () < 0)
		return 1;
	test_vendors ();
	mc_maclist_free ();
	if (failures) {
		fprintf (stderr, "%d failure(s)\n", failures);
		return 1;
	}
	return 0;
}
