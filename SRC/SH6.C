/*
 * MS-DOS SHELL - Data Declarations
 *
 * MS-DOS SHELL - Copyright (c) 1990,3 Data Logic Limited and Charles Forsyth
 *
 * This code is based on (in part) the shell program written by Charles
 * Forsyth and is subject to the following copyright restrictions:
 *
 * 1.  Redistribution and use in source and binary forms are permitted
 *     provided that the above copyright notice is duplicated in the
 *     source form and the copyright notice in file sh6.c is displayed
 *     on entry to the program.
 *
 * 2.  The sources (or parts thereof) or objects generated from the sources
 *     (or parts of sources) cannot be sold under any circumstances.
 *
 *    $Header: /usr/users/istewart/src/shell/sh2.2/RCS/sh6.c,v 2.11 1993/11/09 10:39:49 istewart Exp $
 *
 *    $Log: sh6.c,v $
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
 *	Revision 2.2  1992/09/03  18:54:45  istewart
 *	Beta 213 Updates
 *
 *	Revision 2.1  1992/07/10  10:52:48  istewart
 *	211 Beta updates
 *
 *	Revision 2.0  1992/04/13  17:39:09  Ian_Stewartson
 *	MS-Shell 2.0 Baseline release
 *
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>

#if defined (OS2) || defined (__OS2__)
#  define INCL_DOSSESMGR
#  include <os2.h>
#endif

#include "sh.h"

static char	*Copy_Right1 = "%s SH Version 2.2 (%s) - %s (OS %d.%d)\n";

#if (OS_VERSION == OS_OS2_32)
static char	*Copy_Right2 = "OS/2 (32-Bit)";
#elif (OS_VERSION == OS_OS2_16)
static char	*Copy_Right2 = "OS/2 (16-Bit)";
#elif (OS_VERSION == OS_DOS_32)
static char	*Copy_Right2 = "MS-DOS (32-Bit)";
#else
static char	*Copy_Right2 = "MS-DOS (16-Bit)";
#endif

static char	*Copy_Right3 = "Copyright (c) Data Logic Ltd and Charles Forsyth 1990, 93\n";
#if (OS_VERSION == OS_DOS_32)
bool		IgnoreHardErrors = FALSE;/* Hard Error Flag		*/
#endif
char		**ParameterArray = (char **)NULL; /* Parameter array	*/
int		ParameterCount = 0;	/* # entries in parameter array	*/
int		ExitStatus;		/* Exit status			*/
bool		ExpansionErrorDetected;
				/* interactive (talking-type wireless)	*/
bool		InteractiveFlag = FALSE;
bool		ProcessingEXECCommand;	/* Exec mode			*/
bool		UseConsoleBuffer = FALSE;/* Flag from dofc to		*/
					/* GetConsoleInput		*/
int		AllowMultipleLines;	/* Allow continuation onto	*/
					/* Next line			*/
int		Current_Event = 0;	/* Current history event	*/
bool		ChangeInitLoad = FALSE;	/* Change load .ini pt.		*/
unsigned int	GlobalFlags = 0;	/* Other global flags		*/
int		MaxNumberofFDs = 20;	/* Max no of file descriptors	*/

int		DisabledVariables = 0;	/* Disabled variables		*/
int		StartCursorPosition = -1;/* Start cursor position	*/

#if (OS_VERSION != OS_DOS)
unsigned int	SW_intr;		/* Interrupt flag		*/
bool		IgnoreInterrupts = FALSE;/* Ignore interrupts flag	*/
char		path_line[FFNAME_MAX];	/* Execution path		*/
#else
					/* Swap mode			*/
int		Swap_Mode = SWAP_EXPAND | SWAP_DISK;
#endif

Break_C		*Break_List;	/* Break list for FOR/WHILE		*/
Break_C		*Return_List;	/* Return list for RETURN		*/
Break_C		*SShell_List;	/* SubShell list for EXIT		*/
bool		RestrictedShellFlag = FALSE;	/* Restricted shell	*/
				/* History processing enabled flag	*/
bool		HistoryEnabled = FALSE;

void		*FunctionTree = (void *)NULL;	/* Function tree	*/
FunctionList	*CurrentFunction = (FunctionList *)NULL;
void		*AliasTree = (void *)NULL;	/* Alias tree		*/

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
void		*JobTree = (void *)NULL;	/* job tree		*/
bool		ExitWithJobsActive = FALSE;	/* Exit flag		*/
int		CurrentJob = 0;			/* No current		*/
int		PreviousJob = 0;		/* Previous Job		*/

/*
 * Session Info
 */

char		*SessionEndQName = (char *)NULL;	/* Queue	*/

/*
 * Special flag for EMX parameters
 */

bool		EMXStyleParameters = FALSE;
#endif

/*
 * redirection
 */

Save_IO		*SSave_IO;	/* Save IO array			*/
int		NSave_IO_E = 0;	/* Number of entries in Save IO array	*/
int		MSave_IO_E = 0;	/* Max Number of entries in SSave_IO	*/
S_SubShell	*SubShells;	/* Save Vars array			*/
int		NSubShells = 0;	/* Number of entries in SubShells	*/
int		MSubShells = 0;	/* Max Number of entries in SubShells	*/
int		LastNumberBase = -1;	/* Last base entered		*/

int		InterruptTrapPending;	/* Trap pending			*/
int		Execute_stack_depth;	/* execute function recursion	*/
					/* depth			*/
void		*VariableTree = (void *)NULL;	/* Variable dictionary	*/
VariableList	*CurrentDirectory;	/* Current directory		*/
char		*LastUserPrompt;	/* Last prompt output		*/
char		*LastUserPrompt1;	/* Alternate Last prompt output	*/
char		IFS[] = "IFS";		/* Inter-field separator	*/
char		PS1[] = "PS1";		/* Prompt 1			*/
char		PS2[] = "PS2";		/* Prompt 2			*/
char		PS3[] = "PS3";		/* Prompt 3			*/
char		PS4[] = "PS4";		/* Prompt 4			*/
char		PathLiteral[] = "PATH";
char		HomeVariableName[] = "HOME";
char		ShellVariableName[] = "SHELL";
char		HistoryFileVariable[] = "HISTFILE";
char		HistorySizeVariable[] = "HISTSIZE";
char		*ComspecVariable= "COMSPEC";
char		*ParameterCountVariable = "#";
char		*ShellOptionsVariable = "-";
char		*StatusVariable = "?";
char		SecondsVariable[] = "SECONDS";
char		RandomVariable[] = "RANDOM";
char		LineCountVariable[] = "LINENO";
char		*RootDirectory = "x:/";

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
char		WinTitleVariable[] = "WINTITLE";
#endif

char		*OldPWDVariable = "OLDPWD";
char		*PWDVariable = "PWD";
char		*ENVVariable = "ENV";

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
char		BATExtension[] = ".cmd";
#else
char		BATExtension[] = ".bat";
#endif

char		SHELLExtension[] = ".sh";
char		KSHELLExtension[] = ".ksh";
char		EXEExtension[] = ".exe";
char		COMExtension[] = ".com";
char		*NotFound = "not found";
char		*BasicErrorMessage = "%s: %s";
char		*DirectorySeparator = "/";
char		LastWordVariable[] = "_";
char		OptArgVariable[] = "OPTARG";
char		OptIndVariable[] = "OPTIND";
char		MailCheckVariable[] = "MAILCHECK";
char		FCEditVariable[] = "FCEDIT";
char		EditorVariable[] = "EDITOR";
char		VisualVariable[] = "VISUAL";
char		Trap_DEBUG[] = "~DEBUG";
char		Trap_ERR[] = "~ERR";
char		*LIT_NewLine = "\n";
char		*LIT_BadID = "bad identifier";
char		LIT_export[] = "export";
char		LIT_exit[] = "exit";
char		LIT_exec[] = "exec";
char		LIT_history[] = "history";
char		LIT_REPLY[] = "REPLY";
char		LIT_LINES[] = "LINES";
char		LIT_COLUMNS[] = "COLUMNS";
char		*ListVarFormat = "%s=%s\n";
char		*Outofmemory1 = "Out of memory";
char		*LIT_Emsg = "%s: %s (%s)";
char		*LIT_2Strings = "%s %s";
char		*LIT_3Strings = "%s %s%s";
char		*LIT_SyntaxError = "Syntax error";
char	 	*LIT_BadArray = "%s: bad array value";
char 		*LIT_ArrayRange = "%s: subscript out of range";
char		*LIT_BNumber = "[%d]";
char		*LIT_Invalidfunction = "Invalid function name";
char		LIT_Test[] = "[[";
int		MaximumColumns = 80;	/* Max columns			*/
int		MaximumLines = 25;	/* Max Lines			*/

/*
 * Fopen modes, different between IBM OS/2 2.x and the rest
 */

#ifdef __IBMC__
char		*OpenReadMode = "r";	/* Open file in read mode	*/
char		*OpenWriteMode = "w";	/* Open file in write mode	*/
char		*OpenAppendMode = "w+";	/* Open file in append mode	*/
char		*OpenWriteBinaryMode = "wb";/* Open file in append mode	*/
#else
char		*OpenReadMode = "rt";	/* Open file in read mode	*/
char		*OpenWriteMode = "wt";	/* Open file in write mode	*/
char		*OpenAppendMode = "wt+";/* Open file in append mode	*/
char		*OpenWriteBinaryMode = "wb";/* Open file in append mode	*/
#endif

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
STARTDATA	*SessionControlBlock;	/* Start a session info		*/
#endif

/*
 * Parser information
 */

char			CurrentLexIdentifier [IDENT+1];/* Identifier	*/
Source			*source;	/* yyparse/ScanNextToken source	*/
YYSTYPE			yylval;		/* result from ScanNextToken	*/
int			yynerrs;	/* Parse error flag		*/


/*
 * Global program mode information
 */

ExeMode 	ExecProcessingMode;	/* Global Program mode		*/

/*
 * Character Types array
 */

char		CharTypes [UCHAR_MAX + 1];

/*
 * Modified getopt values
 */

int		OptionIndex = 1;	/* optind			*/
int		OptionStart;		/* start character		*/
char		*OptionArgument;	/* optarg			*/

/*
 * Device directory.  The length of this string is defined by the variable
 * LEN_DEVICE_NAME_HEADER
 */

char		*DeviceNameHeader = "/dev/";

int		MemoryAreaLevel;/* Current allocation area		*/
long		flags = 0L;	/* Command line flags			*/
char		null[] = "";	/* Null value				*/
char		ConsoleLineBuffer[LINE_MAX + 1]; /* Console line buffer	*/

/*
 * Current environment
 */

ShellFileEnvironment	e = {
    (int *)NULL,		/* Error handler			*/
    0L,				/* IO Map for this level		*/
    (char *)NULL,		/* Current line buffer			*/
    (ShellFileEnvironment *)NULL,	/* Previous Env pointer		*/
};

/*
 * Some defines to print version and release info
 */

#define str(s)		# s
#define xstr(s)		str(s)

#if (OS_VERSION != OS_DOS) && (OS_VERSION != OS_DOS_32)
#  define OS_VERS_N	_osmajor / 10
#else
#  define OS_VERS_N	_osmajor
#endif

/*
 * The only bit of code in this module prints the version number
 */

void	PrintVersionNumber (FILE *fp)
{
    fprintf (fp, Copy_Right1, Copy_Right2, xstr (RELEASE), __DATE__,
	     OS_VERS_N, _osminor);
    fputs (Copy_Right3, fp);
}
