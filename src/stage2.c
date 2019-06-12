/*
 * implements the main (second) init stage (stage 2)
 *
 * Simple, easy to understand, and quite small (compared to SysV init or
 * Systemd) init replacement that launches a single child process and respawns
 * it when it dies.
 *
 * Responds to SIGUSR1 by halting the system, SIGUSR2 by powering off, and
 * SIGTERM or SIGINT reboot.
 *
 * TODO:
 *	-R /path/to/bin option to reexec running exe
 *	-S /path/to/sulogin fuer single user mode
 *	-T builtin ttymon
 */

#include "feat.h"
#include <stdlib.h>
#include <limits.h>
#include <sys/types.h>
#include <stdint.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <termios.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include "os.h"

#if defined (OSLinux)
#  include <sys/prctl.h>
#  include <linux/vt.h>
#  include <linux/kd.h>
#endif

#include "version.h"
#include "init.h"
#include "initreq.h"
#include "icommon.c"

/* constants */
enum {
  DO_REBOOT		= 0x01,
  DO_HALT		= 0x02,
  DO_POWEROFF		= 0x04,
} ;

/* global services array */
static struct {
  const void * argv ;
  unsigned int restart ;
  time_t start ;
  pid_t pid ;
} svc [ ] = {
  { NULL, 0, 0, 0 },
} ;

/* function prototypes */
extern int do_reboot ( const int how ) ;

/* map handled termination signals to reboot(2) calls */
static unsigned long int check_sig ( const sig_atomic_t s )
{
  /* see what signal was actually received and act accordingly */
  if ( s & ( 1 << SIGUSR1 ) ) {
    return DO_HALT ;
  } else if ( s & ( 1 << SIGUSR2 ) ) {
    return DO_POWEROFF ;
#ifdef SIGPWR
  } else if ( s & ( 1 << SIGPWR ) ) {
    return DO_POWEROFF ;
#endif
#ifdef SIGWINCH
  } else if ( s & ( 1 << SIGWINCH ) ) {
    return DO_POWEROFF ;
#endif
  } else if ( s & ( 1 << SIGHUP ) ) {
    return DO_REBOOT ;
  } else if ( s & ( 1 << SIGINT ) ) {
    return DO_REBOOT ;
  } else if ( s & ( 1 << SIGTERM ) ) {
    return DO_REBOOT ;
  }

  /* default action (poweroff the machine) */
  return DO_POWEROFF ;
}

static int is_c ( const char * const path )
{
  return ftype ( 0, S_IFCHR, path ) ;
}

/* set up the self pipe */
static int mkpipe ( int p [ 2 ] )
{
#if defined (OSLinux) || defined (OSfreebsd) || defined (OSdragonfly) || deifned (OSopenbsd)
  return pipe2 ( p, O_CLOEXEC | O_NONBLOCK ) ;
#elif defined (OSnetbsd)
  return pipe2 ( p, O_CLOEXEC | O_NONBLOCK | O_NOSIGPIPE ) ;
#else
  register int i = pipe ( p ) ;

  if ( i ) {
    i += fcntl ( p [ 0 ], F_SETFD, FD_CLOEXEC ) ;
    i += fcntl ( p [ 1 ], F_SETFD, FD_CLOEXEC ) ;
    i += fcntl ( p [ 0 ], F_SETFL, O_NONBLOCK ) ;
    i += fcntl ( p [ 1 ], F_SETFL, O_NONBLOCK ) ;
  }

  return i ;
#endif
}

/* creates a new SysV message queue */
static int create_mq ( key_t key, const char create )
{
  if ( create ) {
    int e = 0, mqid = -3 ;
    struct msqid_ds mqs ;

    key = ( 1 > key ) ? 0x01 : key ;

    while ( 0 > ( mqid = msgget ( key, 00600 | IPC_CREAT | IPC_EXCL ) ) )
    {
      e = errno ;
      if ( EEXIST == e ) { (void) msgctl ( mqid, IPC_RMID, NULL ) ; }
      else if ( EACCES == e || ENOSPC == e ) { return -3 ; break ; }
      else { e = 0 ; (void) msleep ( 5, 0 ) ; }
    }

    if ( 0 <= mqid && 0 == msgctl ( mqid, IPC_STAT, & mqs ) ) {
      mqs . msg_perm . mode = 00600 ;
      mqs . msg_perm . uid = getuid () ;
      mqs . msg_perm . gid = getgid () ;
      (void) msgctl ( mqid, IPC_SET, & mqs ) ;
    }

    if ( e && ( 1 > errno ) ) { errno = e ; }
    return mqid ;
  }

  return msgget ( key, 0 ) ;
}

/* accept the signal (SIGWINCH) sent by the kernel when the kbrequest
 * key sequence was typed (on Linux)
 */
#if defined (OSLinux) && defined (SIGWINCH) && defined (KDSIGACCEPT)
static void kbreq ( void )
{
  if ( is_c ( VT_MASTER ) ) {
    const int fd = open ( VT_MASTER, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC ) ;

    if ( 0 <= fd ) {
      if ( isatty ( fd ) ) { (void) ioctl ( fd, KDSIGACCEPT, SIGWINCH ) ; }

      if ( 0 < fd ) { (void) close_fd ( fd ) ; }

      return ;
    }
  }

  if ( isatty ( 0 ) ) { (void) ioctl ( 0, KDSIGACCEPT, SIGWINCH ) ; }
}
#endif

/* stop the given service */
static void svc_stop ( const unsigned int x )
{
  const pid_t p = svc [ x ] . pid ;
  svc [ x ] . pid = 0 ;

  if ( 1 < p ) { (void) kill ( p, SIGTERM ) ; }
}

/* start the given service */
static void svc_start ( const unsigned int x )
{
  const pid_t p = spawn ( 0, (char **) ( svc [ x ] . argv ) ) ;

  if ( 1 < p && 0 == kill ( p, 0 ) ) {
    svc [ x ] . pid = p ;
    ++ svc [ x ] . restart ;

    if ( 1 > svc [ x ] . start ) {
      svc [ x ] . start = time ( NULL ) ;
    }
  }
}

/* restart the given service */
static void svc_restart ( const unsigned int x )
{
  const time_t t = time ( NULL ) ;

  if ( 1 > svc [ x ] . start ) { svc [ x ] . start = t ; }

  /* disable fast respawning services */
  if ( 9 < svc [ x ] . restart && t > svc [ x ] . start &&
    120 > ( t - svc [ x ] . start ) )
  {
    svc_stop ( x ) ;
    (void) fprintf ( stderr,
      "\r\n\r[init] service %d is respawning too fast ...\r\n\r\n", x ) ;
    (void) fflush ( NULL ) ;
  } else {
    svc_start ( x ) ;
  }
}

/* look for services to restart */
static void respawn ( void )
{
  register unsigned int i ;
  pid_t p, q ;

  do {
    p = waitpid ( -1, NULL, WNOHANG ) ;

    if ( 0 == p ) {
      return ;
    } else if ( 0 < p ) {
      i = LEN( svc ) ;

      while ( 0 < i -- ) {
        q = svc [ i ] . pid ;

        if ( 0 < q && q == p ) {
          svc_restart ( i ) ;
          return ;
        }
      }
    }
  } while ( ( 0 < p ) || ( 0 > p && EINTR == errno ) ) ;
}

/* start all services */
static void start ( void )
{
  register unsigned int i = LEN( svc ) ;

  while ( 0 < i -- ) {
    svc [ i ] . pid = 0 ;
    svc [ i ] . restart = 0 ;
    svc [ i ] . start = 0 ;
    svc_start ( i ) ;
  }
}

/* stop all services */
static void stop ( void )
{
  register unsigned int i = LEN( svc ) ;

  while ( 0 < i -- ) { svc_stop ( i ) ; }
}

/* exec into the stage 3 executable */
static int quit ( const unsigned long int f, const char * const ex )
{
  (void) reap ( 1, 0 ) ;
  /* stop all services */
  stop () ;
  (void) msleep ( 0, 50 * 1000 ) ;
  (void) reap ( 1, 0 ) ;

  if ( 1 == getpid () ) {
    char * av [ 3 ] = { (char *) NULL } ;
    av [ 0 ] = "init" ;

    if ( DO_REBOOT & f ) {
      av [ 1 ] = "-r" ;
    } else if ( DO_HALT & f ) {
      av [ 1 ] = "-h" ;
    } else {
      av [ 1 ] = "-p" ;
    }

    (void) chdir ( "/" ) ;
    (void) printf (
      "\r\n\r[init] Trying to exec into stage 3 executable %s ...\r\n\r\n",
      ex ) ;
    (void) fflush ( NULL ) ;
    (void) execve ( ex, av, Env ) ;
    perror ( "execve() failed" ) ;
#if defined (__GLIBC__) && defined (_GNU_SOURCE)
    (void) execvpe ( ex, av, Env ) ;
    perror ( "execvpe() failed" ) ;
#endif
    (void) execvp ( ex, av ) ;
    perror ( "execvp() failed" ) ;
    (void) fflush ( NULL ) ;
    /* start rescue shell here */
    return 111 ;
  }

  return 0 ;
}

static int sig_fd = -1 ;

static void sighand ( int s )
{
  if ( 0 <= sig_fd ) {
    (void) write ( sig_fd, & s, sizeof ( int ) ) ;
  }
}

static void setup_sigs ( void )
{
  struct sigaction sa ;

  /* zero out the sigaction struct before use */
  (void) memset ( & sa, 0, sizeof ( struct sigaction ) ) ;

  sa . sa_flags = SA_RESTART | SA_NOCLDSTOP ;
  sa . sa_handler = sig_child ;
  (void) sigemptyset ( & sa . sa_mask ) ;
  /* catch SIGCHLD */
  (void) sigaction ( SIGCHLD, & sa, NULL ) ;

  /* catch other signals of interest */
  sa . sa_flags = SA_RESTART ;
  sa . sa_handler = sig_misc ;
  (void) sigaction ( SIGTERM, & sa, NULL ) ;
  (void) sigaction ( SIGHUP, & sa, NULL ) ;
  (void) sigaction ( SIGINT, & sa, NULL ) ;
  (void) sigaction ( SIGUSR1, & sa, NULL ) ;
  (void) sigaction ( SIGUSR2, & sa, NULL ) ;
  (void) sigaction ( SIGIO, & sa, NULL ) ;
#ifdef SIGWINCH
  (void) sigaction ( SIGWINCH, & sa, NULL ) ;
#endif
#ifdef SIGPWR
  (void) sigaction ( SIGPWR, & sa, NULL ) ;
#endif

  /* unblock all signals */
  (void) sigprocmask ( SIG_SETMASK, & sa . sa_mask, NULL ) ;
}

int main ( const int argc, char ** argv )
{
  register int i ;
  int sockfd = -1 ;
  int p [ 2 ] ;
  const nfds_t nfds = 1 + NTTYS ;
  struct pollfd pv [ 1 + NTTYS ] ;
  char * ex = STAGE3X ;
  char * pname = ( ( 0 < argc ) && argv && * argv && ** argv ) ?
    * argv : "init" ;
  const pid_t mypid = getpid () ;
  const uid_t myuid = getuid () ;
  extern int opterr, optind, optopt ;
  extern char * optarg ;

  /* initialize global variables */
  sig_fd = -1 ;
  opterr = 1 ;
  /* drop possible privileges we should not have */
  (void) seteuid ( myuid ) ;
  (void) setegid ( getgid () ) ;

  if ( myuid || geteuid () ) {
    (void) fprintf ( stderr, "\r%s:\tmust be super user\r\n", pname ) ;
    (void) fflush ( NULL ) ;
    return 100 ;
  }

  /* parse command line args */
  while ( 0 < ( i = getopt ( argc, argv, ":e:E:hHvVx:X:" ) ) ) {
    switch ( i ) {
      case 'e' :
      case 'E' :
      case 'x' :
      case 'X' :
        if ( optarg && * optarg && is_fnrx ( optarg ) ) {
          ex = optarg ;
        }
        break ;
      case 'h' :
      case 'H' :
        if ( 1 < mypid ) {
          (void) printf ( "\rusage:\t%s [-x /path/to/stage3]\r\n", pname ) ;
          (void) fflush ( NULL ) ;
          return 0 ;
        }
        break ;
      case 'v' :
      case 'V' :
        if ( 1 < mypid ) {
          (void) printf ( "\r%s: peso init stage 2 v%s\r\n",
            pname, PESO_VERSION ) ;
          (void) fflush ( NULL ) ;
          return 0 ;
        }
        break ;
      default :
        break ;
    }
  }

  i = optind ;
  /* set up default environment for subprocesses */
  setup_env () ;
  /* secure default file creation mask */
  (void) umask ( 00022 ) ;
  /* set process resource (upper) limits */
  (void) set_rlimits () ;
  (void) chdir ( "/" ) ;
  (void) setsid () ;
  (void) setpgid ( 0, 0 ) ;
#if defined (OSbsd)
  (void) setlogin ( "root" ) ;
#endif

  /* create the self pipe we will use for signal notifications */
  if ( 0 == mkpipe ( p ) ) {
    pv [ 0 ] = p [ 1 ] ;
    sig_fd = p [ 1 ] ;
  }

  /* set up signal handlers for signals of interest */
  setup_sigs () ;

  /* process #1 specific setup tasks */
  if ( 1 == mypid ) {
#if defined (OSLinux)
    /* set the name of this (calling) thread */
    (void) prctl ( PR_SET_NAME, "init", 0, 0, 0 ) ;
    /* secure attention key sequence (ctrl-alt-del) (sigint) */
    (void) do_reboot ( 'c' ) ;
    /* kbrequest key sequence (sigwinch) */
# if defined (SIGWINCH) && defined (KDSIGACCEPT)
    kbreq () ;
# endif
    /* end Linux */
#elif defined (OSfreebsd)
#elif defined (OSnetbsd)
#elif defined (OSopenbsd)
#endif
  }

  (void) chvt ( 1 ) ;
  /* Ave imperator, morituri te salutant ! */
  (void) printf (
     "\r\n\r%s:\tpeso init stage 2 v%s starting ...\r\n\r\n",
     pname, PESO_VERSION ) ;
  (void) fflush ( NULL ) ;
  (void) reap ( 1, 0 ) ;
  /* start the services */
  start () ;

  /* main "event" loop */
  while ( 0 == got_sig ) {
    /* suspend until handled signals are caught */
    (void) pause () ;

    /* stop looping and enter stage 3 if it was a handled
     * termination signal
     */
    if ( got_sig ) { break ; }

    /* otherwiee SIGCHILD was received and handled */
    respawn () ;
  }

  return quit ( check_sig ( got_sig ), ex ) ;
}

