/*  This file is part of "sshpass", a tool for batch running password ssh authentication
 *  Copyright (C) 2006 Lingnu Open Source Consulting Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version, provided that it was accepted by
 *  Lingnu Open Source Consulting Ltd. as an acceptable license for its
 *  projects. Consult http://www.lingnu.com/licenses.html
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

//
// -- to go with ./configure, enable this and change "#if 1" to "#if 0" below
// 
//  #if HAVE_CONFIG_H
//  #include "config.h"
//  #endif
// 
#if 1
    #define PACKAGE "sshpass-debug"
    // #define PACKAGE_BUGREPORT ""
    #define PACKAGE_NAME PACKAGE
    #define PACKAGE_STRING "sshpass-debug 1.05"
    // #define PACKAGE_TARNAME "sshpass"
    // #define PACKAGE_URL ""
    // #define PACKAGE_VERSION "1.05"

    #if 0
    /* Define as the return type of signal handlers (`int' or `void'). */
    #define RETSIGTYPE void
    /* Define to the type of arg 1 for `select'. */
    #define SELECT_TYPE_ARG1 int
    /* Define to the type of args 2, 3 and 4 for `select'. */
    #define SELECT_TYPE_ARG234 (fd_set *)
    /* Define to the type of arg 5 for `select'. */
    #define SELECT_TYPE_ARG5 (struct timeval *)
    #endif

    /* Define to 1 if you have the ANSI C header files. */
    // #define STDC_HEADERS 1

    // no idea why this is required for correct work, but it is
    #if 0
        /* Enable GNU extensions on systems that have them.  */
        #ifndef _GNU_SOURCE
            # define _GNU_SOURCE 1
        #endif
    #endif

    // this is enough
    #ifndef _XOPEN_SOURCE
        # define _XOPEN_SOURCE 700
    #endif
#endif


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/select.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#if HAVE_TERMIOS_H
#include <termios.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

// timeouts
#include <limits.h>

// debug
#include <syslog.h>
#include <ctype.h>

enum program_return_codes {
    RETURN_NOERROR,
    RETURN_INVALID_ARGUMENTS,
    RETURN_CONFLICTING_ARGUMENTS,
    RETURN_RUNTIME_ERROR,
    RETURN_PARSE_ERRROR,
    RETURN_INCORRECT_PASSWORD,
    RETURN_HOST_KEY_UNKNOWN,
    RETURN_HOST_KEY_CHANGED,
};

// -----------------------------------------------------------------------  

// debug flags
int g_syslog_dbg;
int g_stderr_dbg;
char g_buf_dbg[BUFSIZ];
#define MAXBUF (( sizeof(g_buf_dbg)/sizeof(char) ) - 1 )
#define DBG_NAME_PARENT  "sshpass-dbg-parent"
#define DBG_NAME_CHILD   "sshpass-dbg-child"

void dbg_init( char* name ) {
    if ( NULL != getenv("SSHPASS_DEBUG" ) || NULL != getenv("SSHPASS_DBG_SYSLOG" ) ) {
        g_syslog_dbg = 1;
    } else {
        g_syslog_dbg = 0;
    }
    
    if ( NULL != getenv("SSHPASS_DEBUG" ) || NULL != getenv("SSHPASS_DBG_STDERR" ) ) {
        g_stderr_dbg = 1;
    } else {
        g_stderr_dbg = 0;
    }

    // openlog ("sshpass-dbg", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
    if ( g_syslog_dbg ) {
        name = ( name ) ? ( name ) : ( DBG_NAME_PARENT ) ;
        closelog();
        openlog (name, LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        // syslog (LOG_NOTICE, "Program started by User %d", getuid ());
        // closelog();
    }
}


// prints to either syslog, or stderr, or both; pass -1 for \0-terminated strings
void dbg_text( char* message, int len ) {

    char buf[BUFSIZ];
    int M = BUFSIZ - 1;

    if ( len < 0 ) { len = strlen(message); }

    int N = ( len > M ) ? ( M ) : ( len ) ;
    memcpy(buf, message, N);

    // make it printable
    if (1) {
        int i;
        for( i = 0; i < N; ++i ) { 
            if (!isprint(buf[i])) {
                buf[i] = '*' ;
            }
        }
    }

    if ( g_stderr_dbg ) {
        // openlog ("sshpass-dbg", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        buf[N] = '\n';
        write( 2, buf, N+1 );
        // closelog();
    }
    
    if ( g_syslog_dbg ) {
        // openlog ("sshpass-dbg", LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        buf[N] = '\0';
        syslog (LOG_NOTICE, buf);
        // closelog();
    }
    
}

// -----------------------------------------------------------------------  


// handling read timeout
unsigned int g_read_timeout; // 0 by default => no timeout
void get_fd_timeout() {

    char* timeout_str_seconds = getenv("SSHPASS_READ_TIMEOUT" );

    if ( NULL != timeout_str_seconds ) {
        long timeout = atol( timeout_str_seconds );
        if ( ( timeout <= 0 ) || ( timeout >= INT_MAX ) ) { perror( "atol(); check env/SSHPASS_READ_TIMEOUT: too long, or not an int, etc" ); exit( EXIT_FAILURE ); } 
        g_read_timeout = (unsigned int) timeout ;
    } 
    
    sprintf( g_buf_dbg, "timeout: %u (0 means \"unset\")", g_read_timeout );
    dbg_text( g_buf_dbg, -1 );    
    
} // get_fd_timeout()


void set_read_timeout() {

    /*
     * we shouldn't really need a handler since as master process dies,
     * the controlling tty should be closed by the system and that would notify ssh ;
     * 
     * however, to be on the safe side, one can use a handler
     */

    if ( g_read_timeout ) {
        
        signal( SIGALRM, SIG_DFL ); // no handler
        alarm( g_read_timeout );
    }

} // set_read_timeout()


void unset_read_timeout() {
    
    if ( g_read_timeout ) {
        signal(SIGALRM, SIG_IGN);
    }
    
} // unset_read_timeout()


// -----------------------------------------------------------------------  

// Some systems don't define posix_openpt
#ifndef HAVE_POSIX_OPENPT
int
posix_openpt(int flags)
{
    return open("/dev/ptmx", flags);
}
#endif

int runprogram( int argc, char *argv[] );

struct {
    enum { PWT_STDIN, PWT_FILE, PWT_FD, PWT_PASS } pwtype;
    union {
	const char *filename;
	int fd;
	const char *password;
    } pwsrc;
} args;

static void show_help()
{
    printf("Usage: " PACKAGE_NAME " [-f|-d|-p|-e] [-hV] command parameters\n"
	    "   -f filename   Take password to use from file\n"
	    "   -d number     Use number as file descriptor for getting password\n"
	    "   -p password   Provide password as argument (security unwise)\n"
	    "   -e            Password is passed as env-var \"SSHPASS\"\n"
	    "   With no parameters - password will be taken from stdin\n\n"
	    "   -h            Show help (this screen)\n"
	    "   -V            Print version information\n"
	    "At most one of -f, -d, -p or -e should be used\n");
}

// Parse the command line. Fill in the "args" global struct with the results. Return argv offset
// on success, and a negative number on failure
static int parse_options( int argc, char *argv[] )
{
    int error=-1;
    int opt;

    // Set the default password source to stdin
    args.pwtype=PWT_STDIN;
    args.pwsrc.fd=0;

#define VIRGIN_PWTYPE if( args.pwtype!=PWT_STDIN ) { \
    fprintf(stderr, "Conflicting password source\n"); \
    error=RETURN_CONFLICTING_ARGUMENTS; }

    while( (opt=getopt(argc, argv, "+f:d:p:heV"))!=-1 && error==-1 ) {
	switch( opt ) {
	case 'f':
	    // Password should come from a file
	    VIRGIN_PWTYPE;
	    
	    args.pwtype=PWT_FILE;
	    args.pwsrc.filename=optarg;
	    break;
	case 'd':
	    // Password should come from an open file descriptor
	    VIRGIN_PWTYPE;

	    args.pwtype=PWT_FD;
	    args.pwsrc.fd=atoi(optarg);
	    break;
	case 'p':
	    // Password is given on the command line
	    VIRGIN_PWTYPE;

	    args.pwtype=PWT_PASS;
	    args.pwsrc.password=strdup(optarg);
            
            // Hide the original password from the command line
            {
                int i;

                for( i=0; optarg[i]!='\0'; ++i )
                    optarg[i]='z';
            }
	    break;
	case 'e':
	    VIRGIN_PWTYPE;

	    args.pwtype=PWT_PASS;
	    args.pwsrc.password=getenv("SSHPASS");
            if( args.pwsrc.password==NULL ) {
                fprintf(stderr, "sshpass: -e option given but SSHPASS environment variable not set\n");

                error=RETURN_INVALID_ARGUMENTS;
            }
	    break;
	case '?':
	case ':':
	    error=RETURN_INVALID_ARGUMENTS;
	    break;
	case 'h':
	    error=RETURN_NOERROR;
	    break;
	case 'V':
	    printf("%s (C) 2006-2011 Lingnu Open Source Consulting Ltd.\n"
		    "This program is free software, and can be distributed under the terms of the GPL\n"
		    "See the COPYING file for more information.\n", PACKAGE_STRING );
	    exit(0);
	    break;
	}
    }

    if( error>=0 )
	return -(error+1);
    else
	return optind;
}

int main( int argc, char *argv[] )
{
    int opt_offset=parse_options( argc, argv );

    if( opt_offset<0 ) {
	// There was some error
	show_help();

        return -(opt_offset+1); // -1 becomes 0, -2 becomes 1 etc.
    }

    if( argc-opt_offset<1 ) {
	show_help();

        return 0;
    }

    dbg_init( DBG_NAME_PARENT );
    sprintf(g_buf_dbg, "  ===  Program started by user %d (ppid %d => pid %d)", getuid(), getppid(), getpid());
    dbg_text( g_buf_dbg, -1 );

    get_fd_timeout(); // read env to get SSHPASS_READ_TIMEOUT and set the pointer
    
    return runprogram( argc-opt_offset, argv+opt_offset );
}


int handleoutput( int fd );

/* Global variables so that this information be shared with the signal handler */
static int ourtty; // Our own tty
static int masterpt;

void window_resize_handler(int signum);
void sigchld_handler(int signum);

int runprogram( int argc, char *argv[] )
{
    struct winsize ttysize; // The size of our tty

    // We need to interrupt a select with a SIGCHLD. In order to do so, we need a SIGCHLD handler
    signal( SIGCHLD,sigchld_handler );

    // Create a pseudo terminal for our process
    masterpt=posix_openpt(O_RDWR);

    if( masterpt==-1 ) {
	perror("Failed to get a pseudo terminal");

	return RETURN_RUNTIME_ERROR;
    }

    fcntl(masterpt, F_SETFL, O_NONBLOCK);

    if( grantpt( masterpt )!=0 ) {
	perror("Failed to change pseudo terminal's permission");

	return RETURN_RUNTIME_ERROR;
    }
    if( unlockpt( masterpt )!=0 ) {
	perror("Failed to unlock pseudo terminal");

	return RETURN_RUNTIME_ERROR;
    }

    ourtty=open("/dev/tty", 0);
    if( ourtty!=-1 && ioctl( ourtty, TIOCGWINSZ, &ttysize )==0 ) {
        signal(SIGWINCH, window_resize_handler);

        ioctl( masterpt, TIOCSWINSZ, &ttysize );
    }

    const char *name=ptsname(masterpt);
    int slavept;
    /*
       Comment no. 3.14159

       This comment documents the history of code.

       We need to open the slavept inside the child process, after "setsid", so that it becomes the controlling
       TTY for the process. We do not, otherwise, need the file descriptor open. The original approach was to
       close the fd immediately after, as it is no longer needed.

       It turns out that (at least) the Linux kernel considers a master ptty fd that has no open slave fds
       to be unused, and causes "select" to return with "error on fd". The subsequent read would fail, causing us
       to go into an infinite loop. This is a bug in the kernel, as the fact that a master ptty fd has no slaves
       is not a permenant problem. As long as processes exist that have the slave end as their controlling TTYs,
       new slave fds can be created by opening /dev/tty, which is exactly what ssh is, in fact, doing.

       Our attempt at solving this problem, then, was to have the child process not close its end of the slave
       ptty fd. We do, essentially, leak this fd, but this was a small price to pay. This worked great up until
       openssh version 5.6.

       Openssh version 5.6 looks at all of its open file descriptors, and closes any that it does not know what
       they are for. While entirely within its prerogative, this breaks our fix, causing sshpass to either
       hang, or do the infinite loop again.

       Our solution is to keep the slave end open in both parent AND child, at least until the handshake is
       complete, at which point we no longer need to monitor the TTY anyways.
     */

    int childpid=fork();
    if( childpid==0 ) {
	// Child

        dbg_init( DBG_NAME_CHILD );
        sprintf(g_buf_dbg, "Forked a child (ppid %d => pid %d)", getppid(), getpid());
        dbg_text( g_buf_dbg, -1 );

	// Detach us from the current TTY
	setsid();
        // This line makes the ptty our controlling tty. We do not otherwise need it open
        slavept=open(name, O_RDWR );
        close( slavept );
	
	close( masterpt );

        sprintf( g_buf_dbg, "child: masterpt is %d, slavept is %d ", masterpt, slavept );
        dbg_text( g_buf_dbg, -1 );
        
        // main reason for the lines below is to make argv[] NULL-terminated
        if (1) {

            char **new_argv=malloc(sizeof(char *)*(argc+1));

            int i;

            for( i=0; i<argc; ++i ) {
                new_argv[i]=argv[i];
            }

            new_argv[i]=NULL;

            sprintf( g_buf_dbg, "child is about to run %s ", new_argv[0] );
            dbg_text( g_buf_dbg, -1 );
            execvp( new_argv[0], new_argv );
        }

	perror("sshpass: Failed to run command");

	exit(RETURN_RUNTIME_ERROR);
    } else if( childpid<0 ) {
	perror("sshpass: Failed to create child process");

	return RETURN_RUNTIME_ERROR;
    }
	
    // We are the parent
    slavept=open(name, O_RDWR|O_NOCTTY );

        sprintf( g_buf_dbg, "parent: masterpt is %d, slavept is %d ", masterpt, slavept );
        dbg_text( g_buf_dbg, -1 );

    int status=0;
    int terminate=0;
    pid_t wait_id;
    sigset_t sigmask, sigmask_select;

    // Set the signal mask during the select
    sigemptyset(&sigmask_select);

    // And during the regular run
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGCHLD);

    sigprocmask( SIG_SETMASK, &sigmask, NULL );

    do {
	if( !terminate ) {
	    fd_set readfd;

	    FD_ZERO(&readfd);
	    FD_SET(masterpt, &readfd);

            dbg_text( "parent: select >>>", -1 );
            // alarm(10);
            // signal(SIGALRM, SIG_DFL); // no handler
            set_read_timeout();
	    int selret=pselect( masterpt+1, &readfd, NULL, NULL, NULL, &sigmask_select );
	    // int selret=pselect( masterpt+1, &readfd, NULL, NULL, g_timeout_p, &sigmask_select );
            // signal(SIGALRM, SIG_IGN);
            unset_read_timeout();
            dbg_text( "parent: select <<<", -1 );

	    if( selret>0 ) {
		if( FD_ISSET( masterpt, &readfd ) ) {
                    int ret;
		    if( (ret=handleoutput( masterpt )) ) {
			// Authentication failed or any other error

                        // handleoutput returns positive error number in case of some error, and a negative value
                        // if all that happened is that the slave end of the pt is closed.
                        if( ret ) {
                            close(slavept);
                        }

                        if( ret>0 ) {
                            close( masterpt ); // Signal ssh that it's controlling TTY is now closed
                        }

			terminate=ret;
		    }
		}
	    }
	    wait_id=waitpid( childpid, &status, WNOHANG );
	} else {
	    wait_id=waitpid( childpid, &status, 0 );
	}
    } while( wait_id==0 || (!WIFEXITED( status ) && !WIFSIGNALED( status )) );

    if( terminate>0 )
	return terminate;
    else if( WIFEXITED( status ) )
	return WEXITSTATUS(status);
    else
	return 255;
}

int match( const char *reference, const char *buffer, ssize_t bufsize, int state );
void write_pass( int fd );

int handleoutput( int fd )
{
    // We are looking for the string
    static int prevmatch=0; // If the "password" prompt is repeated, we have the wrong password.
    static int state1, state2;
    static const char compare1[]="assword:"; // Asking for a password
    static const char compare2[]="The authenticity of host "; // Asks to authenticate host
    // static const char compare3[]="WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!"; // Warns about man in the middle attack
    // The remote identification changed error is sent to stderr, not the tty, so we do not handle it.
    // This is not a problem, as ssh exists immediately in such a case
    char buffer[40];
    int ret=0;

    int numread=read(fd, buffer, sizeof(buffer) );
        sprintf( g_buf_dbg, "  >>  parent: !numread %d %s", numread, (numread > 0) ? ":" : "" );
        dbg_text( g_buf_dbg, -1 );
    if( numread<0 ) { // recovered from v. 1.04
        //  // Comment no. 3.1416
        //  // Select is doing a horrid job of waking us up at the right time - it wakes up with "read ready" when the slave
        //  // end of the pty is closed. This result in an IO error when we perform a read. In the general case, this does
        //  // not mean that the master is no more of use, as it may still be that the client will open /dev/tty and send data.
        //  // In our case, we keep the slave end open (leaking a file descriptor - the price you pay for API insanity), and so
        //  // a failure here suggest ssh is ready to exit. 
        return -1;
    }
        dbg_text( buffer, numread );        

    state1=match( compare1, buffer, numread, state1 );

    // Are we at a password prompt?
    if( compare1[state1]=='\0' ) {
	if( !prevmatch ) {
            dbg_text( "write fd >>", -1 ); 
	    write_pass( fd );
            dbg_text( "<< write fd", -1 ); 
	    state1=0;
	    prevmatch=1;
	} else {
	    // Wrong password - terminate with proper error code
	    ret=RETURN_INCORRECT_PASSWORD;
	}
    }

    if( ret==0 ) {
        state2=match( compare2, buffer, numread, state2 );

        // Are we being prompted to authenticate the host?
        if( compare2[state2]=='\0' ) {
            ret=RETURN_HOST_KEY_UNKNOWN;
        }
    }

    return ret;
}

int match( const char *reference, const char *buffer, ssize_t bufsize, int state )
{
    // This is a highly simplistic implementation. It's good enough for matching "Password: ", though.
    int i;
    
    /*
     * should work for the strings with no repetitions in them * *  
     */
    
    for( i=0;reference[state]!='\0' && i<bufsize; ++i ) {
	if( reference[state]==buffer[i] )
	    state++;
	else {
	    state=0;
	    if( reference[state]==buffer[i] )
		state++;
	}
    }

    return state;
}

void write_pass_fd( int srcfd, int dstfd );

void write_pass( int fd )
{
    switch( args.pwtype ) {
    case PWT_STDIN:
	write_pass_fd( STDIN_FILENO, fd );
	break;
    case PWT_FD:
	write_pass_fd( args.pwsrc.fd, fd );
	break;
    case PWT_FILE:
	{
	    int srcfd=open( args.pwsrc.filename, O_RDONLY );
	    if( srcfd!=-1 ) {
		write_pass_fd( srcfd, fd );
		close( srcfd );
	    }
	}
	break;
    case PWT_PASS:
	write( fd, args.pwsrc.password, strlen( args.pwsrc.password ) );
	write( fd, "\n", 1 );
	break;
    }
}

void write_pass_fd( int srcfd, int dstfd )
{

    int done=0;

    while( !done ) {
	char buffer[40];
	int i;
	int numread=read( srcfd, buffer, sizeof(buffer) );
	done=(numread<1);
	for( i=0; i<numread && !done; ++i ) {
	    if( buffer[i]!='\n' )
		write( dstfd, buffer+i, 1 );
	    else
		done=1;
	}
    }

    write( dstfd, "\n", 1 );
}

void window_resize_handler(int signum)
{
    struct winsize ttysize; // The size of our tty

    if( ioctl( ourtty, TIOCGWINSZ, &ttysize )==0 )
        ioctl( masterpt, TIOCSWINSZ, &ttysize );
}

// Do nothing handler - makes sure the select will terminate if the signal arrives, though.
void sigchld_handler(int signum)
{
}
