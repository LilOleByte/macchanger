AM_CPPFLAGS = -DLISTDIR="\"$(datadir)/$(PACKAGE)\""

bin_PROGRAMS = macchanger

macchanger_SOURCES = \
mac.h mac.c \
maclist.h maclist.c \
netinfo.h netinfo.c \
main.c

check_PROGRAMS = mac_test list_test
mac_test_SOURCES = mac.h mac.c mac_test.c
list_test_SOURCES = mac.h mac.c maclist.h maclist.c list_test.c
TESTS = mac_test list_test
