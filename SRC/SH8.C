/*
 * MS-DOS SHELL - Unix File I/O Emulation
 *
 * MS-DOS SHELL - Copyright (c) 1990,3 Data Logic Limited
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
 * The directory functions opendir, readdir, closedir are based on the public
 * domain implementation for MS-DOS written by Michael Rendell
 * ({uunet,utai}michael@garfield), August 1897
 *
 *    $Header: /usr/users/istewart/src/shell/sh2.2/RCS/sh8.c,v 2.11 1993/11/09 10:39:49 istewart Exp $
 *
 *    $Log: sh8.c,v $
 *	Revision 2.11  1993/11/09  10:39:49  istewart
 *	Beta 226 checking
 *
 *	Revision 2.10  1993/08/25  16:03:57  istewart
 *	Beta 225 - see Notes file
 *
 *	Revision 2.9  1993/07/02  10:21:35  istewart
 *	224 Beta fixes
 *
 *	Revision 2.8  1993/06/14  11:00:12  istewart
 *	More changes for 223 beta
 *
 *	Revision 2.7  1993/06/02  09:52:35  istewart
 *	Beta 223 Updates - see Notes file
 *
 *	Revision 2.6  1993/02/16  16:03:15  istewart
 *	Beta 2.22 Release
 *
 *	Revision 2.5  1993/01/26  18:35:09  istewart
 *	Release 2.2 beta 0
 *
 *	Revision 2.4  1992/12/14  10:54:56  istewart
 *	BETA 215 Fixes and 2.1 Release
 *
 *	Revision 2.3  1992/11/06  10:03:44  istewart
 *	214 Beta test updates
 *
 *	Revision 2.2  1992/07/16  14:33:34  istewart
 *	Beta 212 Baseline
 *
 *	Revision 2.1  1992/07/10  10:52:48  istewart
 *	211 Beta updates
 *
 *	Revision 2.0  1992/04/13  17:39:09  Ian_Stewartson
 *	MS-Shell 2.0 Baseline release
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <setjmp.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <ctype.h>

/*
 * There appears to be no alloca in TurboC
 */

#if defined (__TURBOC__)
#  include <alloc.h>
#  include <dir.h>
#  define alloca(x)		malloc (x)
#  define alloca_free(x)	free (x)
#else
#  include <malloc.h>
#  define alloca_free(x)
#endif

#if defined (OS2) || defined (__OS2__)
#  define INCL_DOSFILEMGR
#  define INCL_DOSSESMGR
#  define INCL_DOSMISC
#  define INCL_DOSMEMMGR
#  define INCL_DOSPROCESS
#  define INCL_DOSMODULEMGR
#  include <os2.h>			/* OS2 functions declarations	*/
#else
#  include <dos.h>			/* DOS functions declarations	*/
#  include <bios.h>
#endif

#include "sh.h"

#define MAX_LINE	160		/* Max line length		*/

#define F_START		4
#define	NSTART		16		/* default number of words to	*/
					/* allow for initially		*/
#define IS_OCTAL(a)	(((a) >= '0') && ((a) <= '7'))

static char	*nopipe = "can't create pipe";

/* List of open files to allow us to simulate the Unix open and unlink
 * operation for temporary files
 */

typedef struct flist {
    struct flist	*fl_next;	/* Next link			*/
    char		*fl_name;	/* File name			*/
    bool		fl_close;	/* Delete on close flag		*/
    int			fl_size;	/* Size of fl_fd array		*/
    int			fl_count;	/* Number of entries in array	*/
    int			fl_mode;	/* File open mode		*/
    int			*fl_fd;		/* File ID array (for dup)	*/
} s_flist;

static s_flist			*list_start = (s_flist *)NULL;
static s_flist * F_LOCAL	find_entry (int);
static void F_LOCAL		SaveFileDescriptor (FILE *);
static void F_LOCAL		_Ex_ExpandIndirectFile (char *, Word_B **);
static char * F_LOCAL		_Ex_GetSpace (int, char *);
static char * F_LOCAL		_Ex_SkipWhiteSpace (char *);
static void F_LOCAL		_Ex_ExpandField (char *, Word_B **);
static char * F_LOCAL		_Ex_ConvertEnvVariables (char *);
static void F_LOCAL		_Ex_SaveArgvValue (char *, bool, Word_B **);
static void F_LOCAL		_Ex_CommandLine (char *, Word_B **);

#if (OS_VERSION == OS_OS2_16) || (OS_VERSION == OS_OS2_32)
static void F_LOCAL		_Ex_ProcessEMXArguments (char *, Word_B **);
#endif

#if (OS_VERSION == OS_DOS_32)
extern	char			*_LpCmdLine;
extern	char			*_LpPgmName;
char				*_getdcwd (int, char *, int);
static void F_LOCAL		OutputBIOSString (char *);
#endif

/*
 * Command Line pointers
 */

#if defined (__TURBOC__)	/* Borland C				*/
#  define ARG_ARRAY	_C0argv
#  define ARG_COUNT	_C0argc
#  define ENTRY_POINT	_setargv
#elif defined (__WATCOMC__)	/* WatCom C				*/
#  define ARG_ARRAY	___Argv
#  define ARG_COUNT	___Argc
#  define ENTRY_POINT	__Init_Argv
#elif defined (__IBMC__)	/* IBM C Set/2				*/
#  define ARG_ARRAY	_argv
#  define ARG_COUNT	_argc
#  define ENTRY_POINT	_setuparg
#else				/* Microsoft C				*/
#  define ARG_ARRAY	__argv
#  define ARG_COUNT	__argc
#  define ENTRY_POINT	_setargv
#endif

extern void	ENTRY_POINT (void);
extern char	**ARG_ARRAY; 		/* Current argument address	*/
extern int	ARG_COUNT; 		/* Current argument count	*/

/*
 * General OS independent start of the command line and program name.
 */

char 		*_ACmdLine;
char 		*_APgmName; 		/* Program name			*/

#if defined (OS2)
extern ushort far _aenvseg;
extern ushort far _acmdln;
#endif

/*
 * Directory functions internals structure.
 */

typedef struct _dircontents	DIRCONT;
static void			FreeDirectoryListing (DIRCONT *);

/*
 * Open a file and add it to the Open file list.  Errors are the same as
 * for a normal open.
 */

int S_open (bool d_flag, char *name, int mode)
{
    int			pmask;
    s_flist		*fp = (s_flist *)NULL;
    int			*f_list = (int *)NULL;
    char		*f_name = (char *)NULL;
    void		(*save_signal)(int);
#ifdef __IBMC__
    HFILE		shfFileHandle;
    ULONG		ActionTaken;
    ULONG		ulFileAttribute;
    ULONG		ulOpenFlag;
    ULONG		ulOpenMode;
    APIRET		rc;            /* Return code */
#endif

/* Check this is a valid file name */

    CheckDOSFileName (name);

/* Grap some space.  If it fails, free space and return an error */

    if (((fp = (s_flist *) GetAllocatedSpace (sizeof (s_flist)))
		== (s_flist *)NULL) ||
	((f_list = (int *) GetAllocatedSpace (sizeof (int) * F_START))
		== (int *)NULL) ||
	((f_name = StringSave (name)) == null))
    {
	if (f_list == (int *)NULL)
	    ReleaseMemoryCell ((void *)f_list);

	if (fp == (s_flist *)NULL)
	    ReleaseMemoryCell ((void *)fp);

	return -1;
    }

/* Disable signals */

    save_signal = signal (SIGINT, SIG_IGN);

/* Set up the structure.  Change two Unix device names to the DOS
 * equivalents and disable create
 */

    if (strnicmp (name, DeviceNameHeader, LEN_DEVICE_NAME_HEADER) == 0)
    {
	if (stricmp (&name[5], "tty") == 0)
	    strcpy (&name[5], "con");

	else if (stricmp (&name[5], "null") == 0)
	    strcpy (&name[5], "nul");

	mode &= ~(O_CREAT | O_TRUNC);
    }

    fp->fl_name  = strcpy (f_name, name);
    fp->fl_close = d_flag;
    fp->fl_size  = F_START;
    fp->fl_count = 1;
    fp->fl_fd    = f_list;
    fp->fl_mode  = mode;

/*
 * The IBM C Set/2 OS/2 2.x library function, open, does not appear work
 * correctly.  So I've replaced it with a emulator here
 */

#ifdef __IBMC__

/* Set up open actions */

    if (mode & O_CREAT)
	 ulOpenFlag = OPEN_ACTION_CREATE_IF_NEW |
			((mode & O_EXCL)
			    ? OPEN_ACTION_FAIL_IF_EXISTS
				: ((mode & O_TRUNC)
				    ? OPEN_ACTION_REPLACE_IF_EXISTS
				    : OPEN_ACTION_OPEN_IF_EXISTS));
    else
	 ulOpenFlag = OPEN_ACTION_FAIL_IF_NEW |
	 		((mode & O_TRUNC)
			    ? OPEN_ACTION_REPLACE_IF_EXISTS
			    : OPEN_ACTION_OPEN_IF_EXISTS);

/* Set up open mode */

    ulOpenMode = OPEN_FLAGS_SEQUENTIAL | OPEN_SHARE_DENYNONE;

    if (mode & O_NOINHERIT)
	ulOpenMode |= OPEN_FLAGS_NOINHERIT;

    if (mode & O_WRONLY)
	ulOpenMode |= OPEN_ACCESS_WRITEONLY;

    else if (mode & O_RDONLY)
	ulOpenMode |= OPEN_ACCESS_READONLY;

    else
	ulOpenMode |= OPEN_ACCESS_READWRITE;

    DISABLE_HARD_ERRORS;
    rc = DosOpen (name, &shfFileHandle, &ActionTaken, (ULONG)0, FILE_NORMAL,
    		  ulOpenFlag, ulOpenMode, (PEAOP2)NULL);
    ENABLE_HARD_ERRORS;

    if (!rc)
    {
	if (mode & O_TEXT)
	    setmode (shfFileHandle, O_TEXT);

	else if (mode & O_BINARY)
	    setmode (shfFileHandle, O_BINARY);

	fp->fl_fd[0] = shfFileHandle;
    }

    else
	fp->fl_fd[0] = -1;

#else
    DISABLE_HARD_ERRORS;
    fp->fl_fd[0] = open (name, mode, S_IREAD | S_IWRITE);
    ENABLE_HARD_ERRORS;
#endif

/* Open the file */

    if (fp->fl_fd[0] < 0)
    {
	ReleaseMemoryCell ((void *)f_name);
	ReleaseMemoryCell ((void *)f_list);
	ReleaseMemoryCell ((void *)fp);
	pmask = -1;
    }

/* Make sure everything is in area 0 */

    else
    {
	SetMemoryAreaNumber ((void *)fp, 0);
	SetMemoryAreaNumber ((void *)f_list, 0);

/* List into the list */

	fp->fl_next   = list_start;
	list_start = fp;

/* Return the file descriptor */

	pmask = fp->fl_fd[0];

/* Mark in use */

	ChangeFileDescriptorStatus (pmask, TRUE);
    }

/* Restore signals */

    signal (SIGINT, save_signal);

    return pmask;
}

/*
 * Scan the File list for the appropriate entry for the specified ID
 */

static s_flist * F_LOCAL find_entry (int fid)
{
    s_flist	*fp = list_start;
    int		i;

    while (fp != (s_flist *)NULL)
    {
	for (i = 0; i < fp->fl_count; i++)
	{
	    if (fp->fl_fd[i] == fid)
		return fp;
	}

	fp = fp->fl_next;
    }

    return (s_flist *)NULL;
}

/* Close the file
 *
 * We need a version of close that does everything but close for dup2 as
 * new file id is closed.  If c_flag is TRUE, close the file as well.
 */

int	S_close (int fid, bool c_flag)
{
    s_flist	*fp = find_entry (fid);
    s_flist	*last = (s_flist *)NULL;
    s_flist	*fp1 = list_start;
    int		i;
    bool	release = TRUE;
    bool	delete = FALSE;
    char	*fname;
    void	(*save_signal)(int);

/* Disable signals */

    save_signal = signal (SIGINT, SIG_IGN);

/* Find the entry for this ID */

    if (fp != (s_flist *)NULL)
    {
	for (i = 0; i < fp->fl_count; i++)
	{
	    if (fp->fl_fd[i] == fid)
		fp->fl_fd[i] = -1;

	    if (fp->fl_fd[i] != -1)
		release = FALSE;
	}

/* Are all the Fids closed ? */

	if (release)
	{
	    fname = fp->fl_name;
	    delete = fp->fl_close;
	    ReleaseMemoryCell ((void *)fp->fl_fd);

/* Scan the list and remove the entry */

	    while (fp1 != (s_flist *)NULL)
	    {
		if (fp1 != fp)
		{
		    last = fp1;
		    fp1 = fp1->fl_next;
		    continue;
		}

		if (last == (s_flist *)NULL)
		    list_start = fp->fl_next;

		else
		    last->fl_next = fp->fl_next;

		break;
	    }

/* OK - delete the area */

	    ReleaseMemoryCell ((void *)fp);
	}
    }

/* Flush these two in case they were re-directed */

    FlushStreams ();

/* Close the file anyway */

    if (c_flag)
    {
	i = close (fid);
	ChangeFileDescriptorStatus (fid, FALSE);
    }

/* Delete the file ? */

    if (delete)
    {
	unlink (fname);
	ReleaseMemoryCell ((void *)fname);
    }

/* Restore signals */

    signal (SIGINT, save_signal);

    return i;
}

/*
 * Version of fclose
 */

void	S_fclose (FILE *FP, bool d_flag)
{
    CloseFile (FP);
    S_close (fileno (FP), d_flag);
}

/*
 * Duplicate file handler.  Add the new handler to the ID array for this
 * file.
 */

int S_dup (int old_fid)
{
    int		new_fid;

    if ((new_fid = dup (old_fid)) >= 0)
	S_Remap (old_fid, new_fid);

    return new_fid;
}

/*
 * Add the ID to the ID array for this file
 */

int S_Remap (int old_fid, int new_fid)
{
    s_flist	*fp = find_entry (old_fid);
    int		*flist;
    int		i;

    ChangeFileDescriptorStatus (new_fid, TRUE);

    if (fp == (s_flist *)NULL)
	return new_fid;

/* Is there an empty slot ? */

    for (i = 0; i < fp->fl_count; i++)
    {
	if (fp->fl_fd[i] == -1)
	    return (fp->fl_fd[i] = new_fid);
    }

/* Is there any room at the end ? No - grap somemore space and effect a
 * re-alloc.  What to do if the re-alloc fails - should really get here.
 * Safty check only??
 */

    if (fp->fl_count == fp->fl_size)
    {
	flist = (int *)ReAllocateSpace ((void *)fp->fl_fd,
					(fp->fl_size + F_START) * sizeof (int));

	if (flist == (int *)NULL)
	    return new_fid;

	SetMemoryAreaNumber ((void *)flist, 0);
	fp->fl_fd   = flist;
	fp->fl_size += F_START;
    }

    return (fp->fl_fd[fp->fl_count++] = new_fid);
}


/*
 * Duplicate file handler onto specific handler
 */

int S_dup2 (int old_fid, int new_fid)
{
    int		res = -1;
    int		i;
    Save_IO	*sp;

/* If duping onto stdin, stdout or stderr, Search the Save IO stack for an
 * entry matching us
 */

    if ((new_fid >= STDIN_FILENO) && (new_fid <= STDERR_FILENO))
    {
	for (sp = SSave_IO, i = 0;
	     (i < NSave_IO_E) && (SSave_IO[i].depth < Execute_stack_depth);
	     i++, ++sp)
	    continue;

/* If depth is greater the Execute_stack_depth - we should panic as this
 * should not happen.  However, for the moment, I'll ignore it
 */

/* If there an entry for this depth ? */

	if (i == NSave_IO_E)
	{

/* Do we need more space? */

	    if (NSave_IO_E == MSave_IO_E)
	    {
		sp = (Save_IO *)ReAllocateSpace ((MSave_IO_E != 0)
						    ? SSave_IO : (void *)NULL,
						 (MSave_IO_E + SSAVE_IO_SIZE) *
						    sizeof (Save_IO));

/* Check for error */

		if (sp == (Save_IO *)NULL)
		    return -1;

/* Save original data */

		SetMemoryAreaNumber ((void *)sp, 1);
		SSave_IO = sp;
		MSave_IO_E += SSAVE_IO_SIZE;
	    }

/* Initialise the new entry */

	    sp = &SSave_IO[NSave_IO_E++];
	    sp->depth             = Execute_stack_depth;
	    sp->fp[STDIN_FILENO]  = -1;
	    sp->fp[STDOUT_FILENO] = -1;
	    sp->fp[STDERR_FILENO] = -1;
	}

	if (sp->fp[new_fid] == -1)
	    sp->fp[new_fid] = ReMapIOHandler (new_fid);

	FlushStreams ();
    }

/* OK - Dup the descriptor */

    if ((old_fid != -1) && ((res = dup2 (old_fid, new_fid)) >= 0))
    {
	S_close (new_fid, FALSE);
	res = S_Remap (old_fid, new_fid);
    }

    return res;
}

/*
 * Restore the Stdin, Stdout and Stderr to original values.  If change is
 * FALSE, just remove entries from stack.  A special case for exec.
 */

int RestoreStandardIO (int rv, bool change)
{
    int		j, i;
    Save_IO	*sp;

/* Start at the top and remove any entries above the current execute stack
 * depth
 */

    for (j = NSave_IO_E; j > 0; j--)
    {
       sp = &SSave_IO[j - 1];

       if (sp->depth < Execute_stack_depth)
	   break;

/* Reduce number of entries */

	--NSave_IO_E;

/* If special case (changed at this level) - continue */

	if (!change && (sp->depth == Execute_stack_depth))
	    continue;

/* Close and restore any files */

	for (i = STDIN_FILENO; i <= STDERR_FILENO; i++)
	{
	    if (sp->fp[i] != -1)
	    {
		S_close (i, TRUE);
		dup2 (sp->fp[i], i);
		S_close (sp->fp[i], TRUE);
	    }
	}
    }

    return rv;
}

/*
 * Create a Pipe
 */

int OpenAPipe (void)
{
    int		i;

    if ((i = S_open (TRUE, GenerateTemporaryFileName (), O_PMASK)) < 0)
	PrintErrorMessage (nopipe);

    return i;
}

/*
 * Close a pipe
 */

void CloseThePipe (int pv)
{
    if (pv != -1)
	S_close (pv, TRUE);
}

/*
 * Check for restricted shell
 */

bool CheckForRestrictedShell (char *s)
{
    if (RestrictedShellFlag)
    {
	PrintErrorMessage (BasicErrorMessage, s, "restricted");
	return TRUE;
    }

    return FALSE;
}

/*
 * Check to see if a file is a shell script.  If it is, return the file
 * handler for the file
 */
static char *Extensions[] = { null, SHELLExtension, KSHELLExtension};

int OpenForExecution (char *path, char **params, int *nargs)
{
    int		RetVal = -1;
    int		j;
    int		EndP = strlen (path);
    char	*local_path;

/* Work on a copy of the path */

    if ((local_path = AllocateMemoryCell (EndP + 5)) == (char *)NULL)
	return -1;

    strcpy (local_path, path);

/* Try the file name and then with a .sh and then .ksh appended */

    for (j = 0; j < 3; j++)
    {
        strcpy (&local_path[EndP], Extensions[j]);

	if ((RetVal = CheckForScriptFile (local_path, params, nargs)) >= 0)
	    break;
    }

    ReleaseMemoryCell ((void *)local_path);
    return RetVal;
}

/*
 * Check for shell script
 */

int CheckForScriptFile (char *path, char **params, int *nargs)
{
    char	buf[512];		/* Input buffer			*/
    int		fp;			/* File handler			*/
    int		nbytes;			/* Number of bytes read		*/
    char	*bp;			/* Pointers into buffers	*/
    char	*ep;

    if ((fp = S_open (FALSE, path, O_RMASK)) < 0)
	return -1;

/* zero or less bytes - not a script */

    memset (buf, 0, 512);
    nbytes = read (fp, buf, 512);
    lseek (fp, 0L, SEEK_SET);

    for (ep = &buf[nbytes], bp = buf;
	 (bp < ep) && ((unsigned char)*bp >= 0x08); ++bp)
	continue;

/* If non-ascii file or length is less than 1 - not a script */

    if ((bp != ep) || (nbytes < 1))
    {
	S_close (fp, TRUE);
	return -1;
    }

/* Ensure end of buffer detected */

    buf[511] = 0;

/* Initialise the return parameters, if specified */

    if (params != (char **)NULL)
	*params = null;

    if (nargs != (int *)NULL)
	*nargs = 0;

/* We don't care how many bytes were read now, so use it to count the
 * additional arguments
 */

    nbytes = 0;

/* Find the end of the first line */

    if ((bp = strchr (buf, CHAR_NEW_LINE)) != (char *)NULL)
	*bp = 0;

    bp = buf;
    ep = (char *)NULL;

/* Check for script */

    if ((*(bp++) != CHAR_COMMENT) || (*(bp++) != '!'))
	return fp;

    while (*bp)
    {

/* Save the start of the arguments */

	if (*(bp = _Ex_SkipWhiteSpace (bp)))
	{
	    if (ep == (char *)NULL)
		ep = bp;

/* Count the arguments */

	    ++nbytes;
	}

	bp = SkipToWhiteSpace (bp);
    }

/* Set up the return parameters, if appropriate */

    if ((params != (char **)NULL) && (strlen (ep) != 0))
    {
	if ((*params = AllocateMemoryCell (strlen (ep) + 1)) == (char *)NULL)
	{
	    *params = null;
	    S_close (fp, TRUE);
	    return -1;
	}

	strcpy (*params, ep);
    }

    if (nargs != (int *)NULL)
	*nargs = nbytes;

    return fp;
}

/*
 * Get the file descriptor type.
 */

int	 GetDescriptorType (int fp)
{
#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    OSCALL_PARAM	fsType = HANDTYPE_FILE;
    OSCALL_PARAM	usDeviceAttr;

    if (DosQHandType (fp, &fsType, &usDeviceAttr))
	return DESCRIPTOR_UNKNOWN;

    if (LOBYTE (fsType) == HANDTYPE_PIPE)
        return DESCRIPTOR_PIPE;

    else if (LOBYTE (fsType) != HANDTYPE_DEVICE)
        return DESCRIPTOR_FILE;

/* The value 0x8083 seems to be the value returned by the console */

    else if (usDeviceAttr == 0x8083)
        return DESCRIPTOR_CONSOLE;

    else
        return DESCRIPTOR_DEVICE;
#else
    union  REGS  r;

    r.x.REG_AX = 0x4400;
    r.x.REG_BX = fp;
    intdos (&r, &r);

    if (r.x.cflag)
	return DESCRIPTOR_UNKNOWN;

    if ((r.x.REG_DX & 0x0080) == 0)
        return DESCRIPTOR_FILE;

    return ((r.x.REG_DX & 0x0081) == 0x0081) ? DESCRIPTOR_CONSOLE
    					     : DESCRIPTOR_DEVICE;
#endif
}

/*
 * Get the current drive number
 */

unsigned int GetCurrentDrive (void)
{
#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    OSCALL_PARAM	usDisk;
    ULONG		ulDrives;

    DosQCurDisk (&usDisk, &ulDrives);        /* gets current drive        */

    return (unsigned int)usDisk;
#elif defined (__TURBOC__)
    return (unsigned int)getdisk () + 1;
#else
    unsigned int	CurrentDrive;

    _dos_getdrive (&CurrentDrive);

    return CurrentDrive;
#endif
}

/*
 * Set the current drive number and return the number of drives.
 */

int	SetCurrentDrive (unsigned int drive)
{
#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    OSCALL_RET		usError;
    OSCALL_PARAM	usDisk;
    FSALLOCATE		FsBlock;
    ULONG		ulDrives;
    int			i;

/* Check the drive exists */

    DISABLE_HARD_ERRORS;
    usError = DosQFSInfo (drive, FSIL_ALLOC, (PBYTE)&FsBlock,
    			  sizeof (FSALLOCATE));
    ENABLE_HARD_ERRORS;

    if (usError || DosSelectDisk ((USHORT)drive))
        return -1;

/* Get the current disk and check that to see the number of drives */

    DosQCurDisk (&usDisk, &ulDrives);        /* gets current drive        */

    for (i = 25; (!(ulDrives & (1L << i))) && i >= 0; --i)
	continue;

    return i + 1;

#elif defined (__TURBOC__)
   return setdisk (drive - 1);

#else
    unsigned int	ndrives;

    _dos_setdrive (drive, &ndrives);

    return (int)ndrives;
#endif
}

/*
 * Get and process configuration line:
 *
 * <field> = <field> <field> # comment
 *
 * return the number of fields found.
 */

int	ExtractFieldsFromLine (LineFields *fd)
{
    char	*cp;
    int		len;
    Word_B	*wb;

    if (fgets (fd->Line, fd->LineLength - 1, fd->FP) == (char *)NULL)
    {
	CloseFile (fd->FP);
	return -1;
    }

/* Remove the EOL */

    if ((cp = strchr (fd->Line, CHAR_NEW_LINE)) != (char *)NULL)
	*cp = 0;

/* Remove the comments at end */

    if ((cp = strchr (fd->Line, CHAR_COMMENT)) != (char *)NULL)
	*cp = 0;

/* Extract the fields */

    if (fd->Fields != (Word_B *)NULL)
	fd->Fields->w_nword = 0;

    fd->Fields = SplitString (fd->Line, fd->Fields);

    if (WordBlockSize (fd->Fields) < 2)
	return 1;

/* Check for =.  At end of first field? */

    wb = fd->Fields;
    len = strlen (wb->w_words[0]) - 1;

    if (wb->w_words[0][len] == CHAR_ASSIGN)
	wb->w_words[0][len] = 0;

/* Check second field for just being equals */

    if (strcmp (wb->w_words[1], "=") == 0)
    {
	if ((--(wb->w_nword)) > 1)
	    memcpy (wb->w_words + 1, wb->w_words + 2,
		    (wb->w_nword - 1) * sizeof (void *));
    }

/* Check the second field for starting with an equals */

    else if (*(wb->w_words[1]) == CHAR_ASSIGN)
	strcpy (wb->w_words[1], wb->w_words[1] + 1);

    return wb->w_nword;
}

/*
 * Split the string up into words
 */

Word_B *SplitString (char *string, Word_B *wb)
{
    while (*string)
    {
	while (isspace (*string))
	    *(string++) = 0;

	if (*string)
	    wb = AddWordToBlock (string, wb);

	string = SkipToWhiteSpace (string);
    }

    return wb;
}

/*
 * Test to see if a file is a directory
 */

bool	IsDirectory (char *Name)
{
    struct stat		s;

    return C2bool (S_stat (Name, &s) && S_ISDIR (s.st_mode & S_IFMT));
}

/*
 * Check that filename conforms to DOS format.  Convert additional .'s to ~.
 */

char *CheckDOSFileName (char *name)
{
    char	*s;
    char	*s1;
    int		count = 0;

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    if (!IsHPFSFileSystem (name))
#endif
    {

/* Find start of file name */

	if ((s = strrchr (name, CHAR_UNIX_DIRECTORY)) == (char *)NULL)
	    s = name;

	else
	    ++s;

/* Skip over the directory entries */

	if ((strcmp (s, ".") == 0) || (strcmp (s, "..") == 0))
	    /*SKIP*/;

/* Name starts with a dot? */

	else if (*s == CHAR_PERIOD)
	    count = 2;

/* Count the number of dots */

	else
	{
	    s1 = s;

	    while ((s1 = strchr (s1, CHAR_PERIOD)) != (char *)NULL)
	    {
		count++;
		s1++;
	    }
	}

/* Check the dot count */

	if (count > 1)
	{
	    if (FL_TEST (FLAG_NO_WARNING))
		fprintf (stderr, "sh: File <%s> has too many dots, changed to ",
			 name);

/* Transform the very first if necessary */

	    if (*s == CHAR_PERIOD)
		*s = CHAR_TILDE;

	    s1 = s;
	    count = 0;

/* Convert all except the first */

	    while ((s1 = strchr (s1, CHAR_PERIOD)) != (char *)NULL)
	    {
		if (++count != 1)
		    *s1 = CHAR_TILDE;

		s1++;
	    }

	    if (FL_TEST (FLAG_NO_WARNING))
		PrintWarningMessage ("<%s>", name);
	}
    }

/* Check for double slashes */

    s = name;

    while ((s = strchr (s, CHAR_UNIX_DIRECTORY)) != (char *)NULL)
    {
	if (*(++s) == CHAR_UNIX_DIRECTORY)
	    strcpy (s, s + 1);
    }

    return name;
}

/*
 * Get a valid numeric value
 */

bool	ConvertNumericValue (char *string, long *value, int base)
{
    char	*ep;

    *value = strtol (string, &ep, base);

    return C2bool (!*ep);
}

/*
 * Character types - replaces is???? functions
 */

void	SetCharacterTypes (char *String, int Type)
{
    int		i;

/*
 * If we're changing C_IFS (interfield separators), clear them all
 */

    if ((Type & C_IFS))
    {
	for (i = 0; i < UCHAR_MAX+1; i++)
	    CharTypes[i] &=~ C_IFS;

	CharTypes[0] |= C_IFS;			/* include \0 as an C_IFS */
    }

 /*
  * Allow leading \0 in string
  */

    CharTypes[(unsigned char) *(String++)] |= Type;

    while (*String != 0)
	CharTypes[(unsigned char) *(String++)] |= Type;
}

/*
 * Initialise the Ctypes values
 */

void	InitialiseCharacterTypes ()
{
    int		c;

    memset (CharTypes, 0, UCHAR_MAX);

    for (c = 'a'; c <= 'z'; c++)
	CharTypes[c] = C_ALPHA;

    for (c = 'A'; c <= 'Z'; c++)
	CharTypes[c] = C_ALPHA;

    CharTypes['_'] = C_ALPHA;
    SetCharacterTypes ("0123456789", C_DIGIT);
    SetCharacterTypes ("\0 \t\n|&;<>()", C_LEX1);
    SetCharacterTypes ("*@#!$-?", C_VAR1);
    SetCharacterTypes ("=-+?#%", C_SUBOP);
    SetCharacterTypes ("?*[", C_WILD);
}

/*
 * Word Block Functions
 *
 * Add a new word to a Word Block or list
 */

Word_B *AddWordToBlock (char *wd, Word_B *wb)
{

/* Do we require more space ? */

    if ((wb == (Word_B *)NULL) || (wb->w_nword >= wb->w_bsize))
    {
	int	NewCount = (wb == (Word_B *)NULL) ? NSTART : wb->w_nword * 2;

        if ((wb = ReAllocateSpace (wb, (NewCount * sizeof (char *)) +
					 sizeof (Word_B))) == (Word_B *)NULL)
	    return (Word_B *)NULL;

	wb->w_bsize = NewCount;
    }

/* Add to the list */

    wb->w_words[wb->w_nword++] = (void *)wd;
    return wb;
}

/*
 * Get the number of words in a block
 */

int	WordBlockSize (Word_B *wb)
{
    return (wb == (Word_B *)NULL) ? 0 : wb->w_nword;
}

/*
 * Convert a word block structure into a array of strings
 */

char	**GetWordList (Word_B *wb)
{
    char	**wd;
    int		nb;

/* If the word block is empty or does not exist, return no list */

    if (wb == (Word_B *)NULL)
	return NOWORDS;

/* Get some space for the array and set it up */

    if (((nb = sizeof (char *) * wb->w_nword) == 0) ||
	((wd = (char **)GetAllocatedSpace (nb)) == (char **)NULL))
    {
	ReleaseMemoryCell ((void *)wb);
	return NOWORDS;
    }

    memcpy (wd, wb->w_words, nb);
    ReleaseMemoryCell ((void *)wb);	/* perhaps should done by caller */
    return wd;
}


/*
 * Build up the parameter variables
 */

Word_B *AddParameter (char *value, Word_B *wb, char *function)
{
    char	**NewArray;
    int		Count;
    int		i;

/* Add to block */

    if ((wb = AddWordToBlock (value, wb)) == (Word_B *)NULL)
    {
	fprintf (stderr, BasicErrorMessage, function, Outofmemory1);
	return (Word_B *)NULL;
    }

/* If not end, return */

    if (value != NOWORD)
	return wb;

/* Get number of entries */

    Count = wb->w_nword - 1;

/* Convert to array */

    if ((NewArray = GetWordList (wb)) == NOWORDS)
    {
	fprintf (stderr, BasicErrorMessage, function, Outofmemory1);
	return (Word_B *)NULL;
    }

/* Release old array.  Note: never release entry zero */

    if (ParameterArray != NOWORDS)
    {
	for (i = 1; i < ParameterCount; ++i)
	    ReleaseMemoryCell ((void *)ParameterArray [i]);

	ReleaseMemoryCell ((void *)ParameterArray);
    }

/* Set new array to no-release */

    for (i = 0; i < Count; ++i)
	SetMemoryAreaNumber ((void *)NewArray[i], 0);

    SetMemoryAreaNumber ((void *)NewArray, 0);

/* Reset globals and environment */

    ParameterCount = Count - 1;
    ParameterArray = NewArray;
    SetVariableFromNumeric (ParameterCountVariable, (long)ParameterCount);
    return wb;
}

/*
 * Is this a valid variable name
 */

char	IsValidVariableName (char *s)
{
    if (!IS_VariableFC (*s))
	return *s;

    while (*s && IS_VariableSC (*s))
	++s;

    return *s;
}

/*
 * File Open Stream Save
 */

FILE	*OpenFile (char *name, char *mode)
{
    FILE	*fp;

    DISABLE_HARD_ERRORS;
    fp = fopen (name, mode);
    ENABLE_HARD_ERRORS;

    if (fp != (FILE *)NULL)
    {
        SaveFileDescriptor (fp);
	setvbuf (fp, (char *)NULL, _IOFBF, BUFSIZ);
	ChangeFileDescriptorStatus (fileno (fp), TRUE);
    }

    return fp;
}

/*
 * File D Open Stream Save
 */

FILE	*ReOpenFile (int fid, char *mode)
{
    FILE	*fp = fdopen (fid, mode);

    if (fp != (FILE *)NULL)
    {
        SaveFileDescriptor (fp);
	setvbuf (fp, (char *)NULL, _IOFBF, BUFSIZ);
    }

    return fp;
}

/*
 * File Close Stream Save
 */

int	CloseFile (FILE *fp)
{
    Word_B	*wb = e.OpenStreams;
    int		NEntries = WordBlockSize (wb);
    int		i;

    for (i = 0; i < NEntries; i++)
    {
        if (wb->w_words[i] == (char *)fp)
	    wb->w_words[i] = (char *)NULL;
    }

    ChangeFileDescriptorStatus (fileno (fp), FALSE);
    return fclose (fp);
}

/*
 * Flush output streams
 */

void	FlushStreams (void)
{
    fflush (stdout);
    fflush (stderr);
}

/*
 * Save Descriptor in Environment
 */

static void F_LOCAL SaveFileDescriptor (FILE *fp)
{
    Word_B	*wb = e.OpenStreams;
    int		NEntries = WordBlockSize (wb);
    int		i;

    for (i = 0; i < NEntries; i++)
    {
        if (wb->w_words[i] == (char *)NULL)
	{
	    wb->w_words[i] = (char *)fp;
	    return;
	}
    }

    e.OpenStreams = AddWordToBlock ((char *)fp, wb);
    SetMemoryAreaNumber ((void *)e.OpenStreams, 0);
}

/*
 * Local Stat function to do some additional checks
 */

bool	S_stat (char *FileName, struct stat *Status)
{
    int		rc;

    CheckDOSFileName (FileName);

    DISABLE_HARD_ERRORS;
    rc = stat (FileName, Status);
    ENABLE_HARD_ERRORS;

    return rc ? FALSE : TRUE;
}

/*
 * Local access function to do some additional checks
 */

bool	S_access (char *FileName, int mode)
{
    int		rc;

    CheckDOSFileName (FileName);

    DISABLE_HARD_ERRORS;
    rc = access (FileName, mode);
    ENABLE_HARD_ERRORS;

    return rc ? FALSE : TRUE;
}

/*
 * Local change directory function to do some additional checks
 */

bool	S_chdir (char *PathName)
{
    int		rc;

    CheckDOSFileName (PathName);

    DISABLE_HARD_ERRORS;
    rc = chdir (PathName);
    ENABLE_HARD_ERRORS;

    return rc ? FALSE : TRUE;
}

/*
 * Local get current directory function to do some additional checks
 *
 * Assumes that PathName is a string of length PATH_MAX + 6.
 */

bool	S_getcwd (char *PathName, int drive)
{
    char	*res;

#if defined (__TURBOC__)
    *(strcpy (PathName, RootDirectory)) = GetDriveLetter (drive);
#endif

    DISABLE_HARD_ERRORS;

    if (drive)
#if defined (__TURBOC__)
	res = getcurdir (drive, PathName + 3) ? (char *)NULL : PathName;
#else
	res = _getdcwd (drive, PathName, PATH_MAX + 4);
#endif

    else
	res = getcwd (PathName, PATH_MAX + 4);

    ENABLE_HARD_ERRORS;
    PathName[PATH_MAX + 5] = 0;

/* Convert to Unix format */

    PATH_TO_UNIX (PathName);
    PATH_TO_LOWER_CASE (PathName);

    return (res == (char *)NULL) ? FALSE : TRUE;
}

/*
 * Open the directory stream
 */

DIR * _CDECL	opendir (name)
const char	*name;
{
    DIR			*dirp;
    char		*last;
    DIRCONT		*dp;
    char		*nbuf;

#if (OS_VERSION == OS_OS2_32)
    FILEFINDBUF3 	dtabuf;
#  define DTA_NAME	dtabuf.achName
    HDIR		d_handle = HDIR_SYSTEM;
    ULONG		d_count = 1;
    bool		HPFS = FALSE;
#elif (OS_VERSION == OS_OS2_16)
    FILEFINDBUF 	dtabuf;
#  define DTA_NAME	dtabuf.achName
    HDIR		d_handle = HDIR_SYSTEM;
    USHORT		d_count = 1;
    bool		HPFS = FALSE;
#elif defined (__TURBOC__)
    struct ffblk	dtabuf;
#  define DTA_NAME	dtabuf.ff_name
#else
    struct find_t	dtabuf;
#  define DTA_NAME	dtabuf.name
#endif
    int			len = strlen (name);
    unsigned long	rc;

    if (!len)
    {
	errno = ENOTDIR;
	return (DIR *)NULL;
    }

    if ((nbuf = AllocateMemoryCell (len + 5)) == (char *)NULL)
	return (DIR *) NULL;

    strcpy (nbuf, name);
    last = &nbuf[len - 1];

/* Ok, DOS is very picky about its directory names.  The following are
 * valid.
 *
 *  c:/
 *  c:.
 *  c:name/name1
 *
 *  c:name/ is not valid
 */

    if (((*last == CHAR_DOS_PATH) || (*last == CHAR_UNIX_DIRECTORY)) &&
	(len > 1) && (!((len == 3) && (name[1] == CHAR_DRIVE))))
	*(last--) = 0;

/* Check its a directory and get some space */

    if ((!IsDirectory (nbuf)) ||
        ((dirp = (DIR *) AllocateMemoryCell (sizeof (DIR))) == (DIR *) NULL))
    {
	ReleaseMemoryCell (nbuf);
	return (DIR *)NULL;
    }

/* Set up to find everything */

    if ((*last != CHAR_DOS_PATH) && (*last != CHAR_UNIX_DIRECTORY))
	strcat (last, DirectorySeparator);

    strcat (last, "*.*");

/* For OS/2, find the file system type */

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    HPFS = IsHPFSFileSystem (nbuf);
#endif

    dirp->dd_loc      = 0;
    dirp->dd_cp       = (DIRCONT *) NULL;
    dirp->dd_contents = (DIRCONT *) NULL;

    DISABLE_HARD_ERRORS;

#if (OS_VERSION == OS_OS2_32)
    rc = DosFindFirst (nbuf, &d_handle, OS_FILE_ATTRIBUTES, &dtabuf,
		       sizeof (FILEFINDBUF3), &d_count, FIL_STANDARD);
#elif (OS_VERSION == OS_OS2_16)
    rc = DosFindFirst (nbuf, &d_handle, OS_FILE_ATTRIBUTES, &dtabuf,
		       sizeof (FILEFINDBUF), &d_count, (ULONG)0);
#elif defined (__TURBOC__)
    rc = findfirst (nbuf, &dtabuf, OS_FILE_ATTRIBUTES);
#else
    rc = _dos_findfirst (nbuf, OS_FILE_ATTRIBUTES, &dtabuf);
#endif

    ENABLE_HARD_ERRORS;

    if (rc)
    {
	ReleaseMemoryCell (nbuf);
	ReleaseMemoryCell (dirp);
	return (DIR *) NULL;
    }

    do
    {
	if (((dp = (DIRCONT *) AllocateMemoryCell (sizeof (DIRCONT)))
		 == (DIRCONT *)NULL) ||
	    ((dp->_d_entry = StringCopy (DTA_NAME)) == null))
	{
	    if (dp->_d_entry != (char *)NULL)
		ReleaseMemoryCell ((char *)dp);

	    ReleaseMemoryCell (nbuf);
	    FreeDirectoryListing (dirp->dd_contents);

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
	    DosFindClose (d_handle);
#endif
	    return (DIR *) NULL;
	}

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
	if (!HPFS)
	    strlwr (dp->_d_entry);
#else
	strlwr (dp->_d_entry);
#endif

	if (dirp->dd_contents != (DIRCONT *) NULL)
	    dirp->dd_cp = dirp->dd_cp->_d_next = dp;

	else
	    dirp->dd_contents = dirp->dd_cp = dp;

	dp->_d_next = (DIRCONT *) NULL;

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
	d_count = 1;
    } while (DosFindNext (d_handle, &dtabuf, sizeof (FILEFINDBUF),
			  &d_count) == 0);
#elif defined (__TURBOC__)
    } while (findnext (&dtabuf) == 0);
#else
    } while (_dos_findnext (&dtabuf) == 0);
#endif

    dirp->dd_cp = dirp->dd_contents;
    ReleaseMemoryCell (nbuf);

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
    DosFindClose (d_handle);
#endif

    return dirp;
}


/*
 * Close the directory stream
 */

int _CDECL	closedir (dirp)
DIR		*dirp;
{
    if (dirp != (DIR *)NULL)
    {
	FreeDirectoryListing (dirp->dd_contents);
	ReleaseMemoryCell ((char *)dirp);
    }

    return 0;
}

/*
 * Read the next record from the stream
 */

struct dirent * _CDECL readdir (dirp)
DIR		*dirp;
{
    static struct dirent	dp;

    if ((dirp == (DIR *)NULL) || (dirp->dd_cp == (DIRCONT *) NULL))
	return (struct dirent *) NULL;

    dp.d_reclen = strlen (strcpy (dp.d_name, dirp->dd_cp->_d_entry));
    dp.d_off    = dirp->dd_loc * 32;
    dp.d_ino    = (ino_t)++dirp->dd_loc;
    dirp->dd_cp = dirp->dd_cp->_d_next;

    return &dp;
}

/*
 * Release the internal structure
 */

static void	FreeDirectoryListing (dp)
DIRCONT		*dp;
{
    DIRCONT	*odp;

    while ((odp = dp) != (DIRCONT *)NULL)
    {
	if (dp->_d_entry != (char *)NULL)
	    ReleaseMemoryCell (dp->_d_entry);

	dp = dp->_d_next;
	ReleaseMemoryCell ((char *)odp);
    }
}

/*
 * For OS/2, we need to know if we have to convert to lower case.  This
 * only applies to non-HPFS (FAT, NETWARE etc) file systems.
 */

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
/*
 * Define the know FAT systems
 */

static char	*FATSystems[] = {"FAT", "NETWARE", (char *)NULL};

/*
 * Check for Long filenames
 */

bool		IsHPFSFileSystem (char *directory)
{
    ULONG		lMap;
    BYTE		bData[128];
    BYTE		bName[3];
    int			i;
    char		*FName;
    unsigned long	rc;
    OSCALL_PARAM	cbData;
    OSCALL_PARAM	nDrive;
#if (OS_VERSION == OS_OS2_32)
    PFSQBUFFER2		pFSQ = (PFSQBUFFER2)bData;
#endif

/*
 * Mike tells me there are IFS calls to determine this, but he carn't
 * remember which.  So we read the partition info and check for HPFS.
 */

    if (isalpha (directory[0]) && (directory[1] == CHAR_DRIVE))
	nDrive = toupper (directory[0]) - '@';

    else
	DosQCurDisk (&nDrive, &lMap);

/* Set up the drive name */

    bName[0] = (char) (nDrive + '@');
    bName[1] = CHAR_DRIVE;
    bName[2] = 0;

    cbData = sizeof (bData);

/* Read the info, if we fail - assume non-HPFS */

    DISABLE_HARD_ERRORS;

#  if (OS_VERSION == OS_OS2_32)
    rc = DosQFSAttach (bName, 0, FSAIL_QUERYNAME, pFSQ, &cbData);
#  else
    rc = DosQFSAttach (bName, 0, FSAIL_QUERYNAME, bData, &cbData, 0L);
#  endif

    ENABLE_HARD_ERRORS;

    if (rc)
	return FALSE;

#  if (OS_VERSION == OS_OS2_32)
    FName = pFSQ->szName + pFSQ->cbName + 1;
#  else
    FName = bData + (*((USHORT *) (bData + 2)) + 7);
#  endif

    for (i = 0; FATSystems[i] != (char *)NULL; i++)
    {
        if (stricmp (FName, FATSystems[i]) == 0)
	    return FALSE;
    }

    return TRUE;
}
#endif

/*
 * Get directory on drive x
 */

#if (__WATCOMC__)
char	*_getdcwd (int drive, char *PathName, int len)
{
    char		TPath [PATH_MAX + 3];
#  if (OS_VERSION == OS_DOS_32)
    union REGS		r;

/* This is really a bit of a cheat.  I'm not sure why it works, but is
 * does.  The missing bit is that you really should set the DS register up
 * but that causes a memory violation. SO!!
 */

    r.x.REG_AX = 0x4700;
    r.h.dl = drive;
    r.x.esi = FP_OFF (TPath);

    intdos (&r, &r);

    if (r.x.cflag & INTR_CF)
	return (char *)NULL;
    
#  else
    ULONG	cbBuf = PATH_MAX + 3;

    if (DosQueryCurrentDir (drive, TPath, &cbBuf))
	return (char *)NULL;
#  endif

/* Insert the drive and root directory */

    *(strcpy (PathName, RootDirectory)) = GetDriveLetter (drive);
    return strcat (PathName, TPath);
}
#endif

/*
 *  MODULE ABSTRACT: _setargv
 *
 *  UNIX like command line expansion
 */

/*
 * OS/2 2.x (32-bit) version
 */

#if (OS_VERSION == OS_OS2_32)
void	ENTRY_POINT (void)
{
    APIRET	rc;
    PTIB	ptib;
    PPIB	ppib;
    char	*MName;
    Word_B	*Alist = (Word_B *)NULL;

/* Get the command line and program name */

    if (rc = DosGetInfoBlocks (&ptib, &ppib))
        PrintErrorMessage ("DosGetInfoBlocks: Cannot find command line\n%s",
			   GetOS2SystemErrorMessage (rc));

    if ((MName = GetAllocatedSpace (FFNAME_MAX)) == (char *)NULL)
	PrintErrorMessage (Outofmemory1);

    if (rc = DosQueryModuleName (ppib->pib_hmte, FFNAME_MAX - 1, MName))
        PrintErrorMessage ("DosQueryModuleName: Cannot get program name\n%s",
			   GetOS2SystemErrorMessage (rc));

/* Save the program name and process the command line */

    _APgmName = MName;
    _Ex_ProcessEMXArguments (ppib->pib_pchcmd, &Alist);

/* Terminate */

    _Ex_SaveArgvValue ((char *)NULL, FALSE, &Alist);
}

#elif (OS_VERSION == OS_OS2_16)

/*
 * OS/2 1.x (16-bit) version
 */

void	ENTRY_POINT (void)
{
    char far		*argvp = (char far *)((((long)_aenvseg) << 16));
    ushort		off = _acmdln;
    Word_B		*Alist = (Word_B *)NULL;

    while (--off)
    {
	if (argvp[off - 1] == 0)
 	    break;
    }

/* Add program name */

    _APgmName =  &argvp[off];

    if (argvp[_acmdln] == 0)
	_Ex_SaveArgvValue (_APgmName, TRUE, &Alist);

    else
    {
	argvp += _acmdln;
	_Ex_ProcessEMXArguments (argvp, &Alist);
    }

/* Terminate */

    _Ex_SaveArgvValue ((char *)NULL, FALSE, &Alist);
}

#else

/*
 * MSDOS version
 */

void	ENTRY_POINT (void)
{
    Word_B		*Alist = (Word_B *)NULL;
    char		*s;		/* Temporary string pointer    	*/
#  if (OS_VERSION == OS_DOS)
					/* Set up pointer to command line */
    unsigned int	envs = *(int far *)((((long)_psp) << 16) + 0x02cL);
    union REGS		r;
    unsigned int	Length;

/* For reasons that escape me, MSC 6.0 does sets up _osmajor and _osminor
 * in the command line parser!
 */

    r.h.ah = 0x30;
    intdos (&r, &r);
    _osminor = r.h.ah;
    _osmajor = r.h.al;

/* Check the length */

    _ACmdLine = (char *)((((long)_psp) << 16) + 0x080L);

    if ((Length = (unsigned int)*(_ACmdLine++)) > 127)
	Length = 127;

    _ACmdLine[Length] = 0;

/* Command line can be null or 0x0d terminated - convert to null */

    if ((s = strchr (_ACmdLine, CHAR_RETURN)) != (char *)NULL)
	*s = 0;

/* Get the program name */

    if ((_osmajor <= 2) || (envs == 0))
	s = "unknown";

/* In the case of DOS 3+, we look in the environment space */

    else
    {
	s = (char far *)(((long)envs) << 16);

	while (*s)
	{
	    while (*(s++) != 0)
		continue;
	}

	s += 3;
    }
#  else
    _ACmdLine = _LpCmdLine;
    s = _LpPgmName;
#  endif

/* Add the program name		*/

    _APgmName = s;
    _Ex_SaveArgvValue (s, TRUE, &Alist);
    _Ex_CommandLine (_ACmdLine, &Alist);
    _Ex_SaveArgvValue ((char *)NULL, FALSE, &Alist);
}
#endif

/*
 * Expand the DOS Command line
 */

static void F_LOCAL _Ex_CommandLine (char *argvp, Word_B **Alist)
{
    char	*spos;			/* End of string pointer	*/
    char	*cpos;			/* Start of string pointer	*/
    char	*fn;			/* Extracted file name string	*/

/* Search for next separator */

    spos = argvp;

    while (*(cpos = _Ex_SkipWhiteSpace (spos)))
    {

/* Extract string argument */

	if ((*cpos == CHAR_DOUBLE_QUOTE) || (*cpos == CHAR_SINGLE_QUOTE))
	{
	    spos = cpos + 1;

	    do
	    {
		if ((spos = strchr (spos, *cpos)) != NULL)
		{
		    spos++;
		    if (spos[-2] != CHAR_META)
			break;
		}

		else
		    spos = &spos[strlen (cpos)];

	    } while (*spos);

	    fn	= _Ex_GetSpace (spos - cpos - 2, cpos + 1);
	}

/* Extract normal argument */

	else
	{
	    spos = SkipToWhiteSpace (cpos);
	    fn = _Ex_GetSpace (spos - cpos, cpos);
	}

/* Process argument */

	if (*cpos != CHAR_SINGLE_QUOTE)
	    fn = _Ex_ConvertEnvVariables (fn);

	switch (*cpos)
	{
	    case CHAR_INDIRECT:		/* Expand file 			*/
		_Ex_ExpandIndirectFile (fn, Alist);
		break;

	    case CHAR_DOUBLE_QUOTE:	/* Expand string    		*/
	    case CHAR_SINGLE_QUOTE:
		_Ex_SaveArgvValue (fn, FALSE, Alist);
		break;

	    default:		/* Expand field	    			*/
		_Ex_ExpandField (fn, Alist);
	}

	ReleaseMemoryCell (fn);
    }
}

/* Expand an indirect file Argument */

static void F_LOCAL _Ex_ExpandIndirectFile (char *file, Word_B **Alist)
{
    FILE    	*fp;		/* File descriptor	    		*/
    char	*EoLFound;	/* Pointer				*/
    int		c_maxlen = MAX_LINE;
    char	*line;		/* Line buffer		    		*/
    char	*eolp;

/* If file open fails, expand as a field */

    if ((fp = fopen (file + 1, OpenReadMode)) == (FILE *)NULL)
	PrintErrorMessage ("Cannot open re-direct file - %s (%s)\n",
			   file + 1, strerror (errno));

/* Grab some memory for the line */

    line = (char *)GetAllocatedSpace (c_maxlen);

/* For each line in the file, remove EOF characters and add argument */

    while (fgets (line, c_maxlen, fp) != (char *)NULL)
    {
	EoLFound = strchr (line, CHAR_NEW_LINE);
	eolp = line;

/* Handle continuation characters */

	while (TRUE)
	{

/* Check for a continuation character */

	    if (((EoLFound = strchr (eolp, CHAR_NEW_LINE)) != (char *)NULL) &&
		(*(EoLFound - 1) == CHAR_META))
	    {
		*(EoLFound - 1) = CHAR_NEW_LINE;
		*EoLFound = 0;
		EoLFound = (char *)NULL;
	    }

	    else if (EoLFound == (char *)NULL)
		EoLFound = strchr (line, 0x1a);

	    if (EoLFound != (char *)NULL)
		break;

/* Find the end of the line */

	    c_maxlen = strlen (line);

/* Get some more space */

	    line = (char *)ReAllocateSpace (line, c_maxlen + MAX_LINE);
	    eolp = &line[c_maxlen];

	    if (fgets (eolp, MAX_LINE, fp) == (char *)NULL)
		break;
	}

/* Terminate the line and add it to the argument list */

	if (EoLFound != (char *)NULL)
	    *EoLFound = 0;

	_Ex_SaveArgvValue (line, FALSE, Alist);
    }

    if (ferror(fp))
	PrintErrorMessage ("%s (%s)", file + 1, strerror (errno));

    ReleaseMemoryCell (line);
    fclose (fp);

/* Delete tempoary files */

    if (((line = strrchr (file + 1, CHAR_PERIOD)) != (char *)NULL) &&
	(stricmp (line, ".tmp") == 0))
	unlink (file + 1);			/* Delete it		*/
}

/* Get space for an argument name */

static char * F_LOCAL _Ex_GetSpace (int length, char *in_s)
{
    char	*out_s;			/* Malloced space address       */

/* Copy string for specified length */

    out_s = strncpy ((char *)GetAllocatedSpace (length + 1), in_s, length);
    out_s[length] = 0;
    return (out_s);
}

/*  Skip over spaces */

static char * F_LOCAL _Ex_SkipWhiteSpace (char *a)
{
    while (isspace (*a))
        a++;

    return a;
}

/*  Skip over spaces */

char	*SkipToWhiteSpace (char *a)
{
    while (*a && !isspace (*a))
        a++;

    return a;
}

/* Find the location of meta-characters.  If no meta, add the argument and
 * return NULL.  If meta characters, return position of end of directory
 * name.  If not multiple directories, return -1
 */

static void F_LOCAL _Ex_ExpandField (char *file, Word_B **Alist)
{
    int		i;
    int		j;
    char	*vecp[2];
    char	**FileList;

    if (strpbrk (file, "?*[]\\") == (char *)NULL)
    {
	_Ex_SaveArgvValue (file, FALSE, Alist);
	return;
    }

    vecp[0] = file;
    vecp[1] = (char *)NULL;

    FileList = ExpandWordList (vecp, EXPAND_GLOBBING | EXPAND_TILDE,
			       (ExeMode *)NULL);

    j = CountNumberArguments (FileList);

    for (i = 0; i < j; )
	_Ex_SaveArgvValue (FileList[i++], FALSE, Alist);

    ReleaseAList (FileList);
}

/*
 * Process Environment - note that field is a malloc'ed field
 */

static char * F_LOCAL _Ex_ConvertEnvVariables (char *field)
{
    char	*sp, *cp, *np, *ep;
    char	save;
    int		b_flag;

    sp = field;

/* Replace any $ strings */

    while ((sp = strchr (sp, CHAR_VARIABLE)) != (char *)NULL)
    {

/* If ${...}, find the terminating } */

	if (*(cp = ++sp) == CHAR_OPEN_BRACES)
	{
	    b_flag = 1;
	    ++cp;

	    while (*cp && (*cp != CHAR_CLOSE_BRACES))
		cp++;
	}

/* Else must be $..., find the terminating non-alphanumeric */

	else
	{
	    b_flag = 0;

	    while (isalnum (*cp))
		cp++;
	}

/* Grab the environment variable */

	if (cp == sp)
	    continue;

/* Get its value */

	save = *cp;
	*cp = 0;
	ep = getenv (sp + b_flag);
	*cp = save;

	if (ep != (char *)NULL)
	{
	    np = _Ex_GetSpace (strlen(field) - (cp - sp) + strlen (ep) - 1,
	    		       field);
	    strcpy (&np[sp - field - 1], ep);
	    ReleaseMemoryCell (field);
	    strcpy ((sp = &np[strlen(np)]), cp + b_flag);
	    field = np;
	}
    }

    return field;
}

/*
 * Handle EMX style arguments
 */

#if (OS_VERSION == OS_OS2_16) || (OS_VERSION == OS_OS2_32)
static void F_LOCAL  _Ex_ProcessEMXArguments (char *argvp, Word_B **Alist)
{
    char	*cp;
    char	*s = argvp;

    _Ex_SaveArgvValue (argvp, TRUE, Alist);

    argvp += strlen (argvp) + 1;
    _ACmdLine = argvp;

/*
 * Add support in OS2 version for Eberhard Mattes EMX interface to commands.
 */

    if ((*argvp) && (*(cp = argvp + strlen (argvp) + 1) == CHAR_TILDE) &&
	(strcmp (s, PATH_TO_UNIX (cp + 1)) == 0))
    {

/* Skip over the program name at string 2 to the start of the first
 * argument at string 3
 */

	EMXStyleParameters = TRUE;
	argvp += strlen (argvp) + 1;
	argvp += strlen (argvp) + 1;

	while (*argvp)
	{
	    cp = (*argvp == CHAR_TILDE) ? argvp + 1 : argvp;

	    if (*cp == CHAR_INDIRECT)
		_Ex_ExpandIndirectFile (cp, Alist);

	    else
		_Ex_SaveArgvValue (cp, FALSE, Alist);

	    argvp += strlen (argvp) + 1;
	}
    }

    else
	_Ex_CommandLine (argvp, Alist);
}
#endif

/*
 * Save the next argument in the word block
 */

static void F_LOCAL _Ex_SaveArgvValue (char *value, bool Convert,
				       Word_B **Alist)
{
    if (Convert)
	PATH_TO_UNIX (value);

    *Alist = AddWordToBlock (value == (char *)NULL ? value : StringSave (value),
    			     *Alist);

    if (value == (char *)NULL)
    {
	ARG_ARRAY = GetWordList (*Alist);
	ARG_COUNT = CountNumberArguments (ARG_ARRAY);
    }
}


#if (OS_VERSION == OS_DOS_32)
/*
 * INTERRUPT 24 - ERROR HANDLER - Output message
 *
 * doserror	- Error code
 * devhdr	- Device header address.
 * deverror	-
 *
 *   Bits     Meaning
 *
 *   15		Disk error if false (0).
 *		Other device error if true (1).
 *   14		Not used.
 *   13		"Ignore" response not allowed if false.
 *   12		"Retry" response not allowed if false.
 *   11		"Fail" response not allowed if false. (Note that DOS changes
 *		"fail" to "abort".)
 *   9-10	Location (for disk error)
 *
 *		0 - DOS
 *		1 - File Allocation Table (FAT)
 *		2 - Directory
 *		3 - Data area
 *
 *	 8	Read error if false; write error if true
 *	 0 - 7	Device Number
 */


char	*I24_Errors [] = {
    "Write-protect error",
    "Unknown unit",
    "Drive not ready",
    "Unknown command",
    "CRC error",
    "Bad request structure length",
    "Seek error",
    "Unknown media type",
    "Sector not found",
    "Out of paper",
    "Write fault",
    "Read fault",
    "General failure",
    "Sharing violation",
    "Lock violation",
    "Invalid disk change",
    "FCB unavailable",
    "Sharing buffer overflow",
    "Unknown error"
};

#define I24_ERROR_LAST		((sizeof (I24_Errors) / sizeof (char *)) - 1)
#define I24_ERROR_DEFAULT	((sizeof (I24_Errors) / sizeof (char *)) - 1)

/* 
 * Error locus
 */

char	*I24_Locus [] = {
    "Unknown",
    "Unknown",
    "Block",
    "Network",
    "Serial",
    "Memory",
};

#define I24_LOCUS_LAST		((sizeof (I24_Locus) / sizeof (char *)) - 1)
#define I24_LOCUS_DEFAULT	0

/*
 * Action
 */

char	*I24_Action [] = {
    "None recommended",
    "Retry, then abort or ignore",
    "Retry with delay, then abort or ignore",
    "Correct information supplied",
    "Abort with cleanup",
    "Abort without cleanup",
    "Ignore",
    "Retry after correcting",
};

#define I24_ACTION_LAST		((sizeof (I24_Action) / sizeof (char *)) - 1)
#define I24_ACTION_DEFAULT	0

/*
 * Class
 */

char	*I24_Class [] = {
    "Unknown error",
    "Out of resource",
    "Temporary failure",
    "Authorisation error",
    "MSDOS Internal error",
    "Hardware error",
    "System error",
    "Application error",
    "Item missing",
    "Item invalid",
    "Item interlocked",
    "Media problem"
};

#define I24_CLASS_LAST		((sizeof (I24_Class) / sizeof (char *)) - 1)
#define I24_CLASS_DEFAULT	0

static char	*I24_Space = "\n\r          ";

int far	HardErrorHandler (unsigned int deverror,
			  unsigned int doserror,
			  unsigned int far *devhdr)
{
    char		DeviceName[10];
    static char		MessageBuffer[300];
    char		*mp;
    int			ch;
    struct DOSERROR	Ecodes;

/* If Ignore set, ignore */

    if (IgnoreHardErrors)
	_hardresume (_HARDERR_FAIL);

/* Initialise device name */

    memset (DeviceName, 0, 10);

/* Get extended error codes */

    dosexterr (&Ecodes);

/* Output on message */

    if (deverror & 0x8000)
    {
	memcpy (DeviceName, (((char *)devhdr) + 10), 8);

	if ((mp = strchr (DeviceName, CHAR_SPACE)) == (char *)NULL)
	   mp = DeviceName + 8;

	strcpy (mp, ":");
    }
    
    else
	sprintf (DeviceName, "%c:", (deverror & 0x0ff) + 'A');

    sprintf (MessageBuffer, "\n\r%s when %s %s %s\n\r",
	     (doserror > I24_ERROR_LAST) ? I24_Errors[I24_ERROR_DEFAULT]
					 : I24_Errors [doserror],
	     (deverror & 0x0100) ? "writing" : "reading",
	     (deverror & 0x8000) ? "device " : "disk",
	     DeviceName);

    OutputBIOSString (MessageBuffer);

    sprintf (MessageBuffer, "[Extended Code  : 0x%.4x%sClass : %s%sAction: %s%sLocus : %s device]\n\r",
	     Ecodes.exterror, I24_Space,
	     (Ecodes.class > I24_CLASS_LAST) ? I24_Class[I24_CLASS_DEFAULT]
					     : I24_Class[Ecodes.class],
	     I24_Space,
	     (Ecodes.action > I24_ACTION_LAST) ? I24_Action[I24_ACTION_DEFAULT]
					       : I24_Action[Ecodes.action],
	     I24_Space,
	     (Ecodes.locus > I24_LOCUS_LAST) ? I24_Locus[I24_LOCUS_DEFAULT]
					     : I24_Locus[Ecodes.locus]);

    OutputBIOSString (MessageBuffer);

/* Allowed actions */

    strcpy (MessageBuffer, "Abort");
    if (deverror & 0x2000)
	strcat (MessageBuffer, ", Ignore");

    if (deverror & 0x1000)
	strcat (MessageBuffer, ", Retry");

    if (deverror & 0x0800)
	strcat (MessageBuffer, ", Fail");

    OutputBIOSString (strcat (MessageBuffer, "? "));

/* Use BIOS to get a key. */

    while (TRUE)
    {
	ch = _bios_keybrd (_KEYBRD_READ) & 0x00ff;

	if ((ch = tolower (ch)) == 'a') 
	{
	    OutputBIOSString ("A\n\r");
	    _hardresume (_HARDERR_ABORT);
	}

	else if ((ch == 'i') && (deverror & 0x2000))
	{
	    OutputBIOSString ("I\n\r");
	    _hardresume (_HARDERR_IGNORE);
	}

	else if ((ch == 'r') && (deverror & 0x1000))
	{
	    OutputBIOSString ("R\n\r");
	    _hardresume (_HARDERR_RETRY);
	}

	else if ((ch == 'f') && (deverror & 0x0800))
	{
	    OutputBIOSString ("F\n\r");
#if defined (__WATCOMC__)
	    _hardresume (_HARDERR_FAIL);
#else
	    _hardretn (doserror);
#endif
	}

	OutputBIOSString ("\007");
    }
}

/*
 * Display a string using BIOS interrupt 0x0e (Write TTY).
 */

static void F_LOCAL OutputBIOSString (char *p)
{
    union REGS		r;

    while (*p)
    {
	r.h.ah = 0x0e;
        r.h.al = *(p++);
	SystemInterrupt (0x10, &r, &r);
    }
}
#endif

/*
 * Output to Stderr
 */

int	feputc (int c)
{
    return putc (c, stderr);
}

int	feputs (char *s)
{
    return fputs (s, stderr);
}

/*
 * Convert drives
 */

unsigned int	GetDriveNumber (char letter)
{
    return tolower (letter) - 'a' + 1;
}

char	 	GetDriveLetter (unsigned int drive)
{
    return (char)(drive + 'a' - 1);
}

/*
 * IO Map functions - Set or clear the inuse bit in the environment.
 */

void	ChangeFileDescriptorStatus (int fd, bool InUse)
{
    if (fd < FDBASE)
	return;
    
    else if (InUse)
	e.IOMap |= 1L << (fd - FDBASE);
    
    else
	e.IOMap &= ~(1L << (fd - FDBASE));
}

/*
 * Convert a string to a number
 */

int	 GetNumericValue (char *as)
{
    long	value;

    return ConvertNumericValue (as, &value, 10) ? (int) value : -1;
}


/*
 * ProcessOutputMetaCharacters - Convert an escaped character to a binary value.
 *
 * Returns the binary value and updates the string pointer.
 */

static struct {
    char	Escaped;
    int		NewValue;
} ConvertMetaCharacters [] =
{
    { 'b',  0x08}, { 'f',  0x0c}, { 'v',	0x0b},	    { 'n',  0x0a},
    { 'r',  0x0d}, { 't',  0x09}, { CHAR_META,  CHAR_META}, { 'c',  -1},
    { 0, 0}
};

int ProcessOutputMetaCharacters (char **cp)
{
    int		c_val = **cp;			/* Current character    */
    int		j = 0;

    if (c_val)
        (*cp)++;

/* Process escaped characters */

    while (ConvertMetaCharacters[j].Escaped != 0)
    {
 	if (ConvertMetaCharacters[j].Escaped == (char)c_val)
	    return ConvertMetaCharacters[j].NewValue;

	++j;
    }

/* Check for an octal string */

    if (IS_OCTAL (c_val) && IS_OCTAL (**cp) && IS_OCTAL (*((*cp) + 1)))
    {
	c_val = ((c_val & 0x07) << 6) |
		((**cp & 0x07) << 3)  |
		((*((*cp) + 1) & 0x07));

	(*cp) += 2;
	return c_val;
    }

    return c_val;
}


/*
 * Extract the next path from a string and build a new path from the
 * extracted path and a file name
 */

char *BuildNextFullPathName (char *path_s,	/* Path string		*/
			     char *file_s,	/* File name string	*/
			     char *output_s)	/* Output path		*/
{
    char	*s = output_s;
    int		fsize = 0;

    while (*path_s && (*path_s != CHAR_SEPARATOR) && (fsize++ < FFNAME_MAX))
	*s++ = *path_s++;

    if ((output_s != s) && (*(s - 1) != CHAR_UNIX_DIRECTORY) &&
	(fsize++ < FFNAME_MAX))
	*s++ = CHAR_UNIX_DIRECTORY;

    *s = 0;

    if (file_s != (char *)NULL)
	strncpy (s, file_s, FFNAME_MAX - fsize);

    output_s[FFNAME_MAX - 1] = 0;

    return (*path_s ? ++path_s : (char *)NULL);
}
