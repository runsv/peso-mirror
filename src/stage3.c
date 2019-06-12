/*
 * implements the last init stage (stage 3)
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
#include <termios.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/reboot.h>
#include "os.h"

#if defined (OSbsd)
#endif

#if defined (OSLinux)
# include <paths.h>
# include <sys/prctl.h>
# include <linux/vt.h>
#elif defined (OSsolaris)
# include <siginfo.h>
#endif

#include "version.h"
#include "init.h"
#include "initreq.h"
#include "icommon.c"
#include "icommon2.c"

/* command line flags */
enum {
  FLAG_REBOOT		= 0x04,
  FLAG_HALT		= 0x08,
  FLAG_POWEROFF		= 0x10,
  FLAG_CLIENT		= 0x01,
  FLAG_DIRECT		= 0x02,
} ;

/* exported by/imported from reboot.c
 * calls reboot(2) (Solaris: uadmin(2)) in the platform
 * specific way.
 */
extern int do_reboot ( const int how ) ;

#if defined (OSsolaris)
/* use sigsendset on Solaris to avoid signaling our own process
 * (not really necessary there when running as process #1)
 */
static int kill_all_sol ( const int sig )
{
  if ( 0 <= sig && NSIG > sig ) {
    procset_t pset ;

    pset . p_op = POP_DIFF ;
    pset . p_lidtype = P_ALL ;
    pset . p_ridtype = P_PID ;
    pset . p_rid = P_MYID ;

    return sigsendset ( & pset, sig ) ;
  }

  return -1 ;
}
#endif

/* kill all running processes */
static void kill_all ( const long int u )
{
  /* reap possible zombies before continuing
   * to avoid them being signaled unnecessarily
   */
  (void) reap ( 1, 0 ) ;

  /* nuke everything */
#if defined (OSsolaris)
  (void) kill_all_sol ( SIGTERM ) ;
  (void) kill_all_sol ( SIGHUP ) ;
  (void) kill_all_sol ( SIGCONT ) ;
  (void) kill_all_sol ( SIGKILL ) ;
#else
  /* -1 never signals process #1.
   * on BSD and Linux it also does not signal our own prcess
   * (even if PID > 1, but we should run as process #1 anyway).
   */
  if ( kill ( -1, SIGTERM ) && ( ESRCH == errno ) ) { goto fin ; }

  if ( 0 < u ) { (void) msleep ( 0, u ) ; }

  (void) reap ( 1, 0 ) ;

  if ( kill ( -1, SIGHUP ) && ( ESRCH == errno ) ) { goto fin ; }

  (void) kill ( -1, SIGCONT ) ;

  if ( 0 < u ) { (void) msleep ( 0, u ) ; }

  (void) reap ( 1, 0 ) ;
  (void) kill ( -1, SIGKILL ) ;
#endif

fin :
  /* reap the just created zombie processes */
  (void) reap ( 0, 0 ) ;
}

/* implements the init client */
static int client ( const unsigned long int f )
{
  if ( 0 == kill ( 1, 0 ) ) {
    char how = 'r' ;
    int i = SIGTERM ;

    if ( FLAG_HALT & f ) {
      how = 'h' ;
      i = SIGUSR1 ;
    } else if ( FLAG_HALT & f ) {
      how = 'p' ;
      i = SIGUSR2 ;
    } else {
      how = 'r' ;
      i = SIGTERM ;
    }

    sync () ;

    if ( FLAG_DIRECT & f ) {
      if ( do_reboot ( how ) ) {
        perror ( "reboot() failed" ) ;
        return 111 ;
      } else { return 0 ; }
    } else if ( 0 == kill ( 1, i ) ) { return 0 ; }
  }

  perror ( "kill() failed" ) ;
  return 111 ;
}

/* (v)fork(2) and call reboot(2) in the child process */
static pid_t vfork_reboot ( const int how )
{
  pid_t p = 0 ;

  /*
  (void) fflush ( NULL ) ;
  */

  while ( 0 > ( p = vfork () ) && ENOSYS != errno ) {
    (void) msleep ( 5, 0 ) ;
  }

  if ( 0 == p ) {
    /* child process to call reboot(2) */
    (void) do_reboot ( ( 0 < how ) ? how : 'p' ) ;
    (void) do_reboot ( 'p' ) ;
    (void) do_reboot ( 'h' ) ;
    _exit ( 127 ) ;
  } else if ( 0 < p ) {
    /* parent process */
    return reap ( 0, p ) ;
  }

  return p ;
}

/* reboot(2)s the system in the requested way */
static int sys_down ( const unsigned long int f )
{
  char how = 'p' ;
  pid_t p = getpid () ;

  /* figure out what the caller is about */
  if ( FLAG_REBOOT & f ) {
    how = 'r' ;
  } else if ( FLAG_HALT & f ) {
    how = 'h' ;
  } else {
    how = 'p' ;
  }

  /* process #1 can't call reboot(2) directly because it kills the task that
   * calls it, which causes the kernel to panic before the actual reboot
   * happens. really ? is that still true ?
   */
  if ( 1 < p ) {
    (void) do_reboot ( how ) ;
    (void) do_reboot ( 'p' ) ;
    (void) do_reboot ( 'h' ) ;
  } else if ( 1 == p ) {
#if defined (OSLinux)
    (void) do_reboot ( 'C' ) ;
#endif

    /* (v)fork(2) and call reboot(2) in the child process and
     * waitpid(2) for its termination.
     */
    p = vfork_reboot ( how ) ;

    /* at this point we should not have any child processes anymore.
     * wait(2) anyway, just to make sure.
     */
    (void) reap ( 0, 0 ) ;

    /* run until killed by the kernel */
    while ( 1 ) {
      (void) pause () ;
      (void) msleep ( 3, 0 ) ;
    }
  }

  /* not reached */
  return 0 ;
}

/* enter init stage 3 */
/* stage 3 brings the system down */
static int stage3 ( const unsigned long int f )
{
  int i = 0 ;
  char * s = NULL ;

  /* set up default environment for subprocesses */
  setup_env () ;
  /* secure default file creation mask */
  (void) umask ( 00022 ) ;
  /* set process resource (upper) limits */
  (void) set_rlimits () ;
  sync () ;
  (void) setsid () ;
  (void) setpgid ( 0, 0 ) ;
#if defined (OSbsd)
  (void) setlogin ( "root" ) ;
#endif
  (void) chdir ( "/" ) ;
  setup_sigs () ;
  /* reap any existing zombie processes */
  (void) reap ( 1, 0 ) ;
  sync () ;
  (void) chvt ( 1 ) ;
  (void) printf (
    "\r\n\r[init] Leaving stage 2 and entering stage 3 ...\r\n"
    "\r[init] Running stage 3a tasks from %s first ...\r\n\r\n"
    , STAGE3A ) ;
  (void) run_conf ( 0, STAGE3A ) ;
  (void) fflush ( NULL ) ;
  /* reap/collect possible zombies left over by stage 3a */
  (void) reap ( 1, 0 ) ;
  /* kill all remaining processes */
  kill_all ( 400 * 1000 ) ;
  (void) chdir ( "/" ) ;
  sync () ;
  (void) chvt ( 1 ) ;
  (void) printf (
    "\r\n\r[init] Killed all remaining processes.\r\n"
    "\r[init] Running stage 3b tasks from %s now ...\r\n\r\n"
    , STAGE3B ) ;
/*
usleep ( 50 * 1000 ) ;
sync () ;
umount ( "/boot" ) ;
umount ( "/mnt/win" ) ;
mount ( "/dev/hda2", "/", "ext3", MS_REMOUNT | MS_RDONLY, "barrier=1" ) ;
*/
  /* run the stage 3b tasks now */
  (void) run_conf ( 0, STAGE3B ) ;
  (void) fflush ( NULL ) ;
  /* reap/collect possible zombies left over by stage 3b */
  (void) reap ( 1, 0 ) ;
  /* kill everything */
  kill_all ( 100 * 1000 ) ;
  (void) chdir ( "/" ) ;

  if ( FLAG_HALT & f ) {
    s = "halt" ;
  } else if ( FLAG_POWEROFF & f ) {
    s = "poweroff" ;
  } else if ( FLAG_REBOOT & f ) {
    s = "reboot" ;
  } else {
    s = "reboot" ;
  }

  (void) chvt ( 1 ) ;
  (void) printf ( "\r\n\r[init] Requesting system %s ...\r\n\r\n", s ) ;
  (void) fflush ( NULL ) ;
  sync () ;

  /* close(2) stdio fds */
  for ( i = 0 ; 3 > i ; ++ i ) { (void) close_fd ( i ) ; }

  return sys_down ( f ) ;
}

int main ( const int argc, char ** argv )
{
  int i = 0 ;
  unsigned long int f = 0 ;
  char * s = NULL ;
  char * pname = ( ( 0 < argc ) && argv && * argv && ** argv ) ?
    * argv : "init" ;
  const pid_t mypid = getpid () ;
  const uid_t myuid = getuid () ;
  extern int opterr, optind, optopt ;
  extern char * optarg ;

  /* initialize global variables */
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
  while ( 0 < ( i = getopt ( argc, argv, ":a:A:b:B:cC:eEfFhHpPrRs:S:xX" ) ) ) {
    switch ( i ) {
      case 'a' :
      case 'A' :
        break ;
      case 'b' :
      case 'B' :
        break ;
      case 'C' :
        /* ignore this option for process #1 */
        if ( 1 < mypid ) { f |= FLAG_CLIENT ; }
        break ;
      case 'f' :
      case 'F' :
        /* ignore this option for process #1 */
        if ( 1 < mypid ) { f |= FLAG_CLIENT | FLAG_DIRECT ; }
        break ;
      case 'h' :
      case 'H' :
        f |= FLAG_HALT ;
        break ;
      case 'p' :
      case 'P' :
        f |= FLAG_POWEROFF ;
        break ;
      case 'r' :
      case 'R' :
        f |= FLAG_REBOOT ;
        break ;
      case 'e' :
      case 'E' :
      case 'x' :
      case 'X' :
        if ( optarg && * optarg && is_f ( optarg ) &&
          0 == access ( optarg, R_OK | X_OK ) )
        {
        }
        break ;
      case 's' :
      case 'S' :
        if ( optarg && * optarg ) {
          s = optarg ;
        }
        break ;
      default :
        break ;
    }
  }

  i = optind ;

  if ( ( 1 < mypid ) && ( FLAG_CLIENT & f ) ) {
    return client ( f ) ;
  }

  (void) printf ( "\r\n\r%s:\tpeso init stage3 v%s starting ...\r\n\r\n",
    pname, PESO_VERSION ) ;
  return stage3 ( f ) ;
}

