char *ckzv = "OS/2 file support, 30 Nov 88";
 
/* C K O F I O  --  Kermit file system support for OS/2 systems */
 
/*
 Author: Chris Adie (C.Adie@uk.ac.edinburgh)
 Copyright (C) 1988 Edinburgh University Computing Service
 Permission is granted to any individual or institution to use, copy, or
 redistribute this software so long as it is not sold for profit, provided this
 copyright notice is retained.
*/

#define	INCL_BASE	/* This is needed to pull in the stuff from os2.h */

/* Includes */
 
#include "ckcker.h"			/* Kermit definitions */
#include "ckcdeb.h"			/* Typedefs, debug formats, etc */
#include <ctype.h>			/* Character types */
#include <stdio.h>			/* Standard i/o */
#include <fcntl.h>
#include <io.h>				/* File io function declarations */
#include <process.h>			/* Process-control function declarations */
#include <string.h>			/* String manipulation declarations */
#include <stdlib.h>			/* Standard library declarations */
#include <sys\types.h>
#include <sys\stat.h>
#include <errno.h>			/* errno values */
#include <direct.h>			/* Directory function declarations */
#include <os2.h>			/* This pulls in a whole load of stuff */

/* Defines */

#define MAXNAMLEN	255
#define	R_OK		4
#define	W_OK		2
#define PIPESZ		16384
					/* Size of pipe for ZXCMD */
#define STDOUT		1
					/* The handle, not the stream */
 
/* Some definitions */

extern int binary;

char *ckzsys = " OS/2";			/* Identify the OS */
 
char *DELCMD = "del ";			/* For file deletion */
char *PWDCMD = "cd ";			/* For saying where I am */
char *TYPCMD = "type ";			/* For typing a file */
char *DIRCMD = "dir ";			/* For directory listing */
char *WHOCMD = "";			/* Who's there? */
char *SPACMD = "chkdsk ";		/* For space on disk */
char *SPACM2 = "chkdsk ";		/* For space on disk */

/* Declarations */
 
FILE *fp[ZNFILS] = { 			/* File pointers */
    NULL, NULL, NULL, NULL, NULL, NULL, NULL };
 
#ifdef MAXPATHLEN
#define CWDBL MAXPATHLEN
#else
#define CWDBL 100
#endif
#define MAXWLD	50

static char cwdbuf[CWDBL+1];		/* Buffer for the current directory */
static char nambuf[MAXNAMLEN+2];	/* Buffer for a filename */
static char finddir[MAXNAMLEN+1];	/* Buffer for path of expanded files */
static FILEFINDBUF findbuf[ MAXWLD ];	/* For list of files */
static USHORT fcount;			/* Number of files in wild group */
static PFILEFINDBUF findptr;

static RESULTCODES res;			/* For child process pid and result */


/*
  Functions (n is one of the predefined file numbers from ckcker.h):
 
   zopeni(n,name)   -- Opens an existing file for input.
   zopeno(n,name)   -- Opens a new file for output.
   zclose(n)        -- Closes a file.
   zchdsk(c)        -- Change current disk.
   zchin(n,&c)      -- Gets the next character from an input file.
   zsout(n,s)       -- Write a null-terminated string to output file, buffered.
   zsoutl(n,s)      -- Like zsout, but appends a line terminator.
   zsoutx(n,s,x)    -- Write x characters to output file, unbuffered.
   zchout(n,c)      -- Add a character to an output file, unbuffered.
   zchki(name)      -- Check if named file exists and is readable, return size.
   zchko(name)      -- Check if named file can be created.
   znewn(name,s)    -- Make a new unique file name based on the given name.
   zdelet(name)     -- Delete the named file.
   zxpand(string)   -- Expands the given wildcard string into a list of files.
   znext(string)    -- Returns the next file from the list in "string".
   zxcmd(cmd)       -- Execute the command in a lower fork.
   zclosf()         -- Close input file associated with zxcmd()'s lower fork.
   zrtol(n1,n2)     -- Convert remote filename into local form.
   zltor(n1,n2)     -- Convert local filename into remote form.
   zchdir(dirnam)   -- Change working directory.
   zfindfile(name)  -- Look down PATH for given file name.
   zkself()         -- Kill self, log out own job.
 */

/*  Z K S E L F  --  Kill Self  */
zkself() {				/* For "bye", but no guarantee! */
	exit(0);			/* Kill all threads */
}

/*  Z O P E N I  --  Open an existing file for input. */
zopeni(n,name) int n; char *name; {
    debug(F111," zopeni",name,n);
    debug(F101,"  fp","",(int) fp[n]);
    if (chkfn(n) != 0) return(0);
    if (n == ZSYSFN) {			/* Input from a system function? */
        debug(F110," invoking zxcmd",name,0);
	return(zxcmd(name));		/* Try to fork the command */
    }
    if (n == ZSTDIO) {			/* Standard input? */
	if (isatty(0)) {
	    ermsg("Terminal input not allowed");
	    debug(F110,"zopeni: attempts input from unredirected stdin","",0);
	    return(0);
	}
	fp[ZIFILE]=stdin;
	setmode(fileno(stdin),O_BINARY);
	return(1);
    }
    if (n == ZIFILE) fp[n] = fopen(name,"rb");		/* Binary mode */
    	else fp[n] = fopen(name,"r");			/* Text mode */
    debug(F111," zopeni", name, (int) fp[n]);
    if (fp[n] == NULL) perror("zopeni");
    return((fp[n] != NULL) ? 1 : 0);
}
 
/*  Z O P E N O  --  Open a new file for output.  */
zopeno(n,name) int n; char *name; {
    debug(F111," zopeno",name,n);
    if (chkfn(n) != 0) return(0);
    if ((n == ZCTERM) || (n == ZSTDIO)) {   /* Terminal or standard output */
	fp[ZOFILE] = stdout;
	debug(F101," fp[]=stdout", "", (int) fp[n]);
	return(1);
    }
    if (n == ZOFILE) fp[n] = fopen(name,"wb");		/* Binary mode */
    	else fp[n] = fopen(name,"w");			/* Text mode */
    if (fp[n] == NULL) {
        perror("zopeno can't open");
    }
    debug(F101, " fp[n]", "", (int) fp[n]);
    return((fp[n] != NULL) ? 1 : 0);
}
 
/*  Z C L O S E  --  Close the given file.  */
/*  Returns 0 if arg out of range, 1 if successful, -1 if close failed.  */
zclose(n) int n; {
    int x;
    if (chkfn(n) < 1) return(0);	/* Check range of n */
    if ((n == ZIFILE) && fp[ZSYSFN]) {	/* If system function */
    	x = zclosf();			/* do it specially */
    } else {
    	if ((fp[n] != stdout) && (fp[n] != stdin)) x = fclose(fp[n]);
	fp[n] = NULL;
    }
    return((x == EOF) ? -1 : 1);
}
 
/*  Z C H I N  --  Get a character from the input file.  */
/*  Returns -1 if EOF, 0 otherwise with character returned in argument  */
zchin(n,c) int n; char *c; {
    int a;
    if (chkfn(n) < 1) return(-1);
    a = getc(fp[n]);
    if (a == EOF) return(-1);		 /* Real end of file */
    if (!binary && a==0x1A) return(-1);	 /* Ctrl-Z marks eof for text mode*/
    *c = a & 0377;
    return(0);
}

/*  Z C H D S K  --  Change currently selected disk */
/* Returns -1 if error, otherwise 0 */
zchdsk(c) char c; {
    USHORT i = toupper(c) - 64;
    return( DosSelectDisk(i) ? 0 : -1 );
}

/*  Z S O U T  --  Write a string to the given file, buffered.  */
zsout(n,s) int n; char *s; {
    if (chkfn(n) < 1) return(-1);
    fputs(s,fp[n]);
    return(0);
}
 
/*  Z S O U T L  --  Write string to file, with line terminator, buffered  */
zsoutl(n,s) int n; char *s; {
    if (chkfn(n) < 1) return(-1);
    fputs(s,fp[n]);
    fputs("\r\n",fp[n]);
    return(0);
}
 
/*  Z S O U T X  --  Write x characters to file, unbuffered.  */
zsoutx(n,s,x) int n, x; char *s; {
    if (chkfn(n) < 1) return(-1);
    return(write(fileno(fp[n]),s,x));
}
 
 
/*  Z C H O U T  --  Add a character to the given file.  */
/*  Should return 0 or greater on success, -1 on failure (e.g. disk full)  */
zchout(n,c) int n; char c; {
    if (chkfn(n) < 1) return(-1);
    if (n == ZSFILE)
    	return(write(fileno(fp[n]),&c,1)); /* Use unbuffered for session log */
    else {				/* Buffered for everything else */
	if (putc(c,fp[n]) == EOF)	/* If true, maybe there was an error */
	    return(ferror(fp[n])?-1:0);	/* Check to make sure */
	else				/* Otherwise... */
	    return(0);			/* There was no error. */
    }
}

/*  C H K F N  --  Internal function to verify file number is ok  */
/*
 Returns:
  -1: File number n is out of range
   0: n is in range, but file is not open
   1: n in range and file is open
*/
chkfn(n) int n; {
    switch (n) {
	case ZCTERM:
	case ZSTDIO:
	case ZIFILE:
	case ZOFILE:
	case ZDFILE:
	case ZTFILE:
	case ZPFILE:
	case ZSFILE:
	case ZSYSFN: break;
	default:
	    debug(F101,"chkfn: file number out of range","",n);
	    fprintf(stderr,"?File number out of range - %d\n",n);
	    return(-1);
    }
    return( (fp[n] == NULL) ? 0 : 1 );
}

/*  Z C H K I  --  Check if input file exists and is readable  */
/*
  Returns:
   >= 0 if the file can be read (returns the size).
     -1 if file doesn't exist or can't be accessed,
     -2 if file exists but is not readable (e.g. a directory file).
     -3 if file exists but protected against read access.
*/
long
zchki(name) char *name; {
    struct stat buf;
    int x; long y;
    x = stat(name,&buf);
    if (x < 0) {
	debug(F111,"zchki stat fails",name,errno);
	return(-1);
    }
    x = buf.st_mode & S_IFMT;		/* Isolate file format field */
    if ((x != 0) && (x != S_IFREG)) {
	debug(F111,"zchki skipping:",name,x);
	return(-2);
    }
    debug(F111,"zchki stat ok:",name,x);
    if ((x = access(name,R_OK)) < 0) { 	/* Is the file accessible? */
	debug(F111," access failed:",name,x); /* No */
    	return(-3);			
    } else {
	y = buf.st_size;
	debug(F111," access ok:",name,(int) y); /* Yes */
	return( (y > -1) ? y : 0 );
    }
}

/*  Z C H K O  --  Check if output file can be created  */
/*
 Returns -1 if write permission for the file would be denied, 0 otherwise.
*/
zchko(name) char *name; {
    int x;
    if (name[0]=='\0') return(-1);		/* If no filename, fail. */
    x = access(name,W_OK);
    if (x < 0  &&  errno==EACCES) {
        debug(F111,"zchko access failed:",name,errno);
	return(-1);
    } else {
	debug(F111,"zchko access ok:",name,x);
	return(0);
    }
}

/*  Z D E L E T  --  Delete the named file.  */
zdelet(name) char *name; {
    unlink(name);
}
 
/*  Z R T O L  --  Convert remote filename into local form  */
zrtol(name,name2) char *name, *name2; {
char *s,*r;
int i;
    i = 0;
    s = name;
    r = name2;
    while ( *s && *s!='.' && i<8 ) {
    	*r++ = *s++;
    	i++;
    }
    *r++='.';
    if (*s && *s!='.') {		/* Name too long */
    	s++;
    	while (*s && *s!='.') s++;
    }
    if (*s=='.') {
    	s++;
    	i = 0;
    	while (*s && i<3) {
    	    *r++=*s++;
    	    i++;
    	}
    }
    *r++='\0';
}
 
/*  Z L T O R  --  Local TO Remote */
/*  Convert filename from local format to common (remote) form.  */
zltor(name,name2) char *name, *name2; {
    char work[100], *cp, *pp;
    debug(F110,"zltor",name,0);
    pp = work;
    for (cp = name; *cp != '\0'; cp++) {	/* strip path name */
    	if ((*cp == '\\') || (*cp == ':')) pp = work;
	else if (islower(*cp)) *pp++ = toupper(*cp); /* Uppercase letters */
	else if (isupper(*cp) || isdigit(*cp) || (*cp == '.')) *pp++ = *cp;
	else *pp++='X';
    }
    *pp = '\0';				/* Tie it off. */
    strcpy(name2,work);
    debug(F110," name2",name2,0);
}
 
/*  Z C H D I R  --  Change directory  */
zchdir(dirnam) char *dirnam; {
	return((chdir(dirnam) == 0) ? 1 : 0);
}
 
/*  Z G T D I R  --  Return pointer to user's current directory  */
char *
zgtdir() {
    return(getcwd(cwdbuf,CWDBL));
}

/*  Z X C M D -- Run a system command so its output can be read like a file */
zxcmd(cmnd) char *cmnd; {
    HFILE newstdout, childout;
    HFILE piperdhdl, pipewrthdl;
    char failname[80], command[255], name[14];
    PCH env;
    USHORT selenv;
    USHORT junkoffset;
    char *p1, *p2;
    int i;
    
    if (cmnd == WHOCMD) return(0);	/* Won't do WHO command */
    debug(F110,"zxcmd",cmnd,0);
	/* Create the pipe */
    if (i=DosMakePipe(&piperdhdl,&pipewrthdl,PIPESZ)) { /* Failed */
        debug(F101,"DosMakePipe error ","",i);
    	return(0);
    }
    newstdout = 0xFFFF;
    	/* New handle for our standard output */
    if (i=DosDupHandle(STDOUT,&newstdout)) {
    	debug(F101,"DosDupHandle error ","",i);
    	return(0);
    }
	/* Close the old one */
    if (i=DosClose(STDOUT)) {
    	debug(F101,"DosClose error ","",i);
    	return(0);
    }
    	/* Make stdout a duplicate of pipe write handle */
    childout = STDOUT;
    if (i=DosDupHandle(pipewrthdl,&childout)) {
    	debug(F101,"DosDupHandle error ","",i);
    	return(0);
    }
	/* Close the old one */
    if (i=DosClose(pipewrthdl)) {
    	debug(F101,"DosClose error ","",i);
    	return(0);
    }
/* Prepare to call DosExecProg */
    DosGetEnv(&selenv,&junkoffset);
    env = MAKEP(selenv,0);
    strcpy(name,"CMD.EXE");
    strcpy(command,name);
    p1=command+strlen(command)+1;
    strcpy(p1,"/c ");
    p1+=3;
    p2=cmnd;
    while ((*p1++ = *p2++) != '\0') ;
    *p1++ = '\0';
    *p1++ = '\0';			/* Must end with double 0 */
    if (i=DosExecPgm(failname,80,EXEC_ASYNC,command,env,&res,name)) {
    	debug(F101,"DosExecPgm error ","",i);
    	return(0);
    }
	/* Close duplicate pipe write handle */
    if (i=DosClose(STDOUT)) {
    	debug(F101,"DosClose error ","",i);
    	return(0);
    }
/* Restore stdout to standard output */
    childout = STDOUT;
    if (i=DosDupHandle(newstdout,&childout)) {
    	debug(F101,"DosDupHandle error ","",i);
    	return(0);
    }
    if (i=DosClose(newstdout)) {
    	debug(F101,"DosClose error ","",i);
    	return(0);
    }
    fp[ZIFILE] = fdopen(piperdhdl,"rb");
    fp[ZSYSFN] = fp[ZIFILE];
    return(1);
}
 
/*  Z C L O S F  - wait for child process to terminate and close the pipe. */
zclosf() {
    USHORT pid;
    int i;
    
    DosCwait(DCWA_PROCESSTREE,DCWW_WAIT,&res,&pid,res.codeTerminate);
    fclose(fp[ZIFILE]);			/* Delete the pipe */
    fp[ZIFILE] = fp[ZSYSFN] = NULL;
    return(1);
}

/*  Z X P A N D  --  Expand a wildcard string into an array of strings  */
/*
  Returns the number of files that match fn1, with data structures set up
  so that first file (if any) will be returned by the next znext() call.
  Returns -1 if insufficient room in the array.
*/
zxpand(fn) char *fn; {
    char *p, *q;
    HDIR dirhdl = 1;

    fcount = MAXWLD;
    findptr=findbuf;
    if (DosFindFirst(fn, &dirhdl, 0, findbuf,
    	MAXWLD*sizeof(FILEFINDBUF), &fcount, 0L)) fcount=0;
    if (fcount == MAXWLD) return(-1);	/* Insufficient space in array */
    strcpy(finddir,fn);
    p=q=finddir;
    while (*p) {
    	*p = toupper(*p);	/* Make it upper case */
    	if (*p == ':') q=p+1;
    	else if (*p == '\\') q=p+1;	/* Get path into finddir */
    	p++;
    }
    *q='\0';
    return(fcount);
}
 
 
/*  Z N E X T  --  Get name of next file from list created by zxpand(). */
/*
 Returns >0 if there's another file, with its name copied into the arg string,
 or 0 if no more files in list.  The name returned includes a path.
*/
znext(fn) char *fn; {
    strcpy(fn,finddir);
    if (fcount-- > 0) strcat(fn,findptr->achName);
    else *fn = '\0';
    (char *) findptr += 24+strlen(findptr->achName);
    debug(F111,"znext",fn,fcount+1);
    return(fcount+1);
}
 
 
/*  Z N E W N  --  Make a new name for the given file  */
 
znewn(fn,s) char *fn, **s; {
    static char buf[MAXNAMLEN];
    char *bp, *xp, *yp, *zp, ch, temp[14];
    int d, i, n;
    xp = bp = buf;
    while (*fn) {			/* Copy name into buf */
	ch = *bp++ = *fn++;
	if ((ch == '\\') || (ch == ':')) xp=bp;
    }
    *bp = '\0';
    yp = xp;
    i = 1;
    while (*yp && (*yp != '.')) {
	yp++;
	if (++i<=6) zp=yp;
    }
    /* zp points to the sixth character in the name, or yp, which ever occurs
       first.  */
    strcpy(temp,yp);			/* Copy extension, if any */
    while (zp != xp+8) {
    	if ( zp < xp+5 ) *zp++='0';
    	else *zp++='?';			/* Pad out with wild cards */
    }
    strcpy(zp,temp);			/* Get the extension back */
    n = zxpand(buf);			/* Expand the resulting wild name */
    d = 0;				/* Index number */
    while (znext(temp)) {
    	i = atoi(temp+5);
    	if (i > d) d = i;
    }
    sprintf(temp,"%03d",d+1);		/* get the number into a string */
    xp[5]=temp[0];
    xp[6]=temp[1];
    xp[7]=temp[2];
    *s = buf;
}

/*  Z F I N D F I L E  --  Look down the PATH for a file */

char *zfindfile(name) char *name; {
static char fname[255];

	if (DosSearchPath(3,"PATH",name,fname,255)) *fname = '\0';
	return(fname);
}
