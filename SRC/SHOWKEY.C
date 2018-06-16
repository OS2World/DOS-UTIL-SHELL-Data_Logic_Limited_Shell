/* MS-DOS SHELL - Show Scan codes
 *
 * MS-DOS SHELL - Copyright (c) 1990,1,2 Data Logic Limited.
 *
 * This code is subject to the following copyright restrictions:
 *
 * 1.  Redistribution and use in source and binary forms are permitted
 *     provided that the above copyright notice is duplicated in the
 *     source form and the copyright notice in file sh6.c is displayed
 *     on entry to the program.
 *
 * 2.  The sources (or parts thereof) or objects generated from the sources
 *     (or parts of sources) cannot be sold under any circumstances.
 *
 *    $Header: /usr/users/istewart/src/shell/sh2.2/RCS/showkey.c,v 2.2 1993/06/14 10:59:58 istewart Exp $
 *
 *    $Log: showkey.c,v $
 * Revision 2.2  1993/06/14  10:59:58  istewart
 * More changes for 223 beta
 *
 * Revision 2.1  1993/06/02  09:54:34  istewart
 * Beta 223 Updates - see Notes file
 *
 * Revision 2.0  1992/07/16  14:35:08  istewart
 * Release 2.0
 *
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <ctype.h>

#if defined (OS2) || defined (__OS2__)
#  define INCL_KBD
#  include <os2.h>

#  if defined (__OS2__)
#    include <bsedev.h>
#  endif

#else
#  include <dos.h>
#endif

void main (void)
{
#if defined (OS2) || defined (__OS2__)
    KBDKEYINFO		kbci;
#else
    union REGS	Key;
    union REGS	Shift;
#endif

    puts ("Control C to terminate");

    while (TRUE)
    {
#if defined (OS2) || defined (__OS2__)

	KbdCharIn (&kbci, IO_WAIT, 0);

	printf ("Scan = 0x%.4x ", kbci.chScan);
	printf ("ASCII = 0x%.4x (%c) ", kbci.chChar, isprint (kbci.chChar) ?
		kbci.chChar : '.');

	printf ("Shift = 0x%.4x ( ", kbci.fsState);

	if (kbci.fsState & RIGHTSHIFT)
	    printf ("RIGHTSHIFT ");

	if (kbci.fsState & LEFTSHIFT)
	    printf ("LEFTSHIFT ");

	if (kbci.fsState & ALT)
	    printf ("ALT ");

	if (kbci.fsState & LEFTALT)
	    printf ("LEFTALT ");

	if (kbci.fsState & RIGHTALT)
	    printf ("RIGHTALT ");

	if (kbci.fsState & CONTROL)
	    printf ("CONTROL ");

	if (kbci.fsState & LEFTCONTROL)
	    printf ("LEFTCONTROL ");

	if (kbci.fsState & RIGHTCONTROL)
	    printf ("RIGHTCONTROL ");

	if (kbci.fsState & SCROLLLOCK_ON)
	    printf ("SCROLLLOCK_ON ");

	if (kbci.fsState & SCROLLLOCK)
	    printf ("SCROLLLOCK ");

	if (kbci.fsState & NUMLOCK_ON)
	    printf ("NUMLOCK_ON ");

	if (kbci.fsState & NUMLOCK)
	    printf ("NUMLOCK ");

	if (kbci.fsState & CAPSLOCK_ON)
	    printf ("CAPSLOCK_ON ");

	if (kbci.fsState & CAPSLOCK)
	    printf ("CAPSLOCK ");

	if (kbci.fsState & INSERT_ON)
	    printf ("INSERT_ON ");

	if (kbci.fsState & SYSREQ)
	    printf ("SYSREQ ");

	puts (")");

	if (kbci.chChar == 0x03)
	    exit (0);
#else
	Key.x.ax = 0;
	int86 (0x16, &Key, &Key);

	Shift.x.ax = 0x0200;
	int86 (0x16, &Shift, &Shift);

	printf ("Scan = 0x%.4x ", Key.h.ah);
	printf ("ASCII = 0x%.4x (%c) ", Key.h.al, isprint (Key.h.al) ?
		Key.h.al : '.');
	printf ("Shift = 0x%.4x ( ", Shift.h.al);

	if (Shift.h.al & 0x01)
	    printf ("Right Shift ");

	if (Shift.h.al & 0x02)
	    printf ("Left Shift ");

	if (Shift.h.al & 0x04)
	    printf ("Control ");

	if (Shift.h.al & 0x08)
	    printf ("Alt ");

	puts (")");

	if (Key.h.al == 0x03)
	    exit (0);
#endif
    }
}
