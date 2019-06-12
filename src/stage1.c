/*
 * implements the first init stage ("stage 1")
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
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <alloca.h>
#include "os.h"

#if defined (OSbsd)
#endif

#if defined (OSLinux)
# include <sys/prctl.h>
# include <linux/vt.h>
#elif defined (OSfreebsd)
#elif defined (OSnetbsd)
#elif defined (OSopenbsd)
#endif

#include "version.h"
#include "init.h"
#include "icommon.c"
#include "icommon2.c"

/* command line flags */
enum {
  FLAG_CHROOT		= 0x01,
} ;

static int is_c ( const char * const path )
{
  return ftype ( 0, S_IFCHR, path ) ;
}

/* see if a given directory is a mountpoint */
static int is_mount_point ( const char * const path )
{
  if ( path && * path ) {
    struct stat st ;

    if ( ( 0 == lstat ( path, & st ) ) && S_ISDIR( st . st_mode ) ) {
      if (
#if defined (OSLinux)
        0 < st . st_ino && 4 > st . st_ino
#else
        /* this is traditional, and what FreeBSD/PC-BSD does.
         * on-disc volumes on Linux mostly do this, too.
         */
        2 == st . st_ino
#endif
      ) { return 1 ; }
      else {
        unsigned int i ;
        struct stat st2 ;
        char buf [ 256 ] = { 0 } ;
        const size_t s = sizeof ( buf ) - 5 ;

        for ( i = 0 ; s > i && '\0' != path [ i ] ; ++ i ) {
          buf [ i ] = path [ i ] ;
        }

        buf [ i ++ ] = '/' ;
        buf [ i ++ ] = '.' ;
        buf [ i ++ ] = '.' ;
        buf [ i ] = '\0' ;

        if ( ( 0 == lstat ( buf, & st2 ) ) && S_ISDIR( st2 . st_mode ) ) {
          if ( st2 . st_dev != st . st_dev ) { return 1 ; }
          /* this is true for the root dir "/" */
          else if ( st2 . st_ino == st . st_ino ) { return 1 ; }
        }
      }
    }
  }

  return 0 ;
}

static void do_mount (
  const mode_t m, const char * const src, const char * const dest,
  const char * const type, const unsigned long int f, const char * const opts )
{
  int i, r = 0 ;

  if ( 0 == is_d ( dest ) ) { (void) mkdir ( dest, m ) ; }

  if ( is_mount_point ( dest ) ) {
    (void) printf ( "\r[init] %s is already a mount point\r\n", dest ) ;
    return ;
  }

  if ( mount ( src, dest, type, f, opts ) ) {
    perror ( "mount() failed" ) ;
    (void) printf ( "\r[init] Cannot mount %s on %s\r\n", src, dest ) ;
  } else {
    (void) printf ( "\r[init] Mounted %s on %s\r\n", src, dest ) ;
  }

  (void) fflush ( NULL ) ;
}

static void mount_pseudofs ( void )
{
  /* order is important here ! */
  do_mount ( 000755, "proc", "/proc", "proc", MS_NODEV | MS_NOSUID, NULL ) ;
  do_mount ( 000755, "sysfs", "/sys", "sysfs", MS_NODEV | MS_NOEXEC | MS_NOSUID, NULL ) ;
  do_mount ( 000755, "tmpfs", "/run", "tmpfs", MS_NODEV | MS_NOSUID, "mode=0755,size=10M" ) ;
  do_mount ( 000755, "devtmpfs", "/dev", "devtmpfs", MS_NOSUID, "mode=0755,size=10M" ) ;
  do_mount ( 000755, "devpts", "/dev/pts", "devpts", MS_NOEXEC | MS_NOSUID,
#ifdef TTY_GID
    TTY_GID
#endif
    "mode=0620" ) ;
  do_mount ( 001777, "mqueue", "/dev/mqueue", "mqueue", MS_NOEXEC | MS_NODEV | MS_NOSUID, NULL ) ;
  do_mount ( 001777, "tmpfs", "/dev/shm", "tmpfs", MS_NOEXEC | MS_NODEV | MS_NOSUID, NULL ) ;

  /* compatibility bind mounts */
  do_mount ( 001777, "/dev/mqueue", "/run/mqueue", NULL, MS_BIND, NULL ) ;
  do_mount ( 001777, "/dev/shm", "/run/shm", NULL, MS_BIND, NULL ) ;
  do_mount ( 000755, "/run", "/var/run", NULL, MS_BIND | MS_REC, NULL ) ;
}

/* "seed" the devfs mounted on /dev */
static void seed_dev ( void )
{
  if ( 0 == is_d ( "/dev" ) ) {
    (void) mkdir ( "/dev", 00755 ) ;
  }

  if ( 0 == is_d ( "/dev/pts" ) ) {
    (void) mkdir ( "/dev/pts", 00755 ) ;
  }

  if ( 0 == is_d ( "/dev/shm" ) ) {
    (void) mkdir ( "/dev/shm", 00755 ) ;
  }

  if ( 0 == is_d ( "/dev/mqueue" ) ) {
    (void) mkdir ( "/dev/mqueue", 00755 ) ;
  }

  /*
  (void) chmod ( "/dev", 00755 ) ;
  (void) chmod ( "/dev/pts", 00755 ) ;
  */

  if ( 0 == is_c ( "/dev/null" ) ) {
    (void) mknod ( "/dev/null", S_IFCHR | 00666, makedev( 1, 3 ) ) ;
  }

  if ( 0 == is_c ( "/dev/kmsg" ) ) {
    (void) mknod ( "/dev/kmsg", S_IFCHR | 00660, makedev( 1, 11 ) ) ;
  }

  if ( 0 == is_c ( CONSOLE ) ) {
    (void) mknod ( CONSOLE, S_IFCHR | 00600, makedev( 5, 1 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty" ) ) {
    (void) mknod ( "/dev/tty", S_IFCHR | 00666, makedev( 5, 0 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty0" ) ) {
    (void) mknod ( "/dev/tty0", S_IFCHR | 00600, makedev( 4, 0 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty1" ) ) {
    (void) mknod ( "/dev/tty1", S_IFCHR | 00620, makedev( 4, 1 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty2" ) ) {
    (void) mknod ( "/dev/tty2", S_IFCHR | 00620, makedev( 4, 2 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty3" ) ) {
    (void) mknod ( "/dev/tty3", S_IFCHR | 00620, makedev( 4, 3 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty4" ) ) {
    (void) mknod ( "/dev/tty4", S_IFCHR | 00620, makedev( 4, 4 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty5" ) ) {
    (void) mknod ( "/dev/tty5", S_IFCHR | 00620, makedev( 4, 5 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty6" ) ) {
    (void) mknod ( "/dev/tty6", S_IFCHR | 00620, makedev( 4, 6 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty7" ) ) {
    (void) mknod ( "/dev/tty7", S_IFCHR | 00620, makedev( 4, 7 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty8" ) ) {
    (void) mknod ( "/dev/tty8", S_IFCHR | 00620, makedev( 4, 8 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty9" ) ) {
    (void) mknod ( "/dev/tty9", S_IFCHR | 00620, makedev( 4, 9 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty10" ) ) {
    (void) mknod ( "/dev/tty10", S_IFCHR | 00620, makedev( 4, 10 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty11" ) ) {
    (void) mknod ( "/dev/tty11", S_IFCHR | 00620, makedev( 4, 11 ) ) ;
  }

  if ( 0 == is_c ( "/dev/tty12" ) ) {
    (void) mknod ( "/dev/tty12", S_IFCHR | 00620, makedev( 4, 12 ) ) ;
  }

  if ( access ( "/dev/fd", F_OK ) ) {
    (void) symlink ( "/proc/self/fd", "/dev/fd" ) ;
  }

  if ( access ( "/dev/stdin", F_OK ) ) {
    (void) symlink ( "/proc/self/fd/0", "/dev/stdin" ) ;
  }

  if ( access ( "/dev/stdout", F_OK ) ) {
    (void) symlink ( "/proc/self/fd/1", "/dev/stdout" ) ;
  }

  if ( access ( "/dev/stderr", F_OK ) ) {
    (void) symlink ( "/proc/self/fd/2", "/dev/stderr" ) ;
  }

  if ( 0 == access ( "/proc/kcore", F_OK ) &&
    0 != access ( "/dev/core", F_OK ) )
  {
    (void) symlink ( "/proc/kcore", "/dev/core" ) ;
  }

  (void) chmod ( CONSOLE, 00600 ) ;
  (void) chmod ( "/dev/null", 00666 ) ;
}

/* "seed" the tmpfs mounted on /run */
static void seed_run ( void )
{
  int i ;

  (void) mkdir ( "/run/openrc", 00755 ) ;
  (void) mkdir ( "/run/perp", 00755 ) ;
  (void) mkdir ( "/run/utmps", 00755 ) ;
  (void) mkdir ( "/run/service", 00755 ) ;
  (void) mkdir ( "/run/sv", 00755 ) ;
  (void) mkdir ( "/run/sv/encore", 00755 ) ;
  (void) mkdir ( "/run/sv/perp", 00755 ) ;
  (void) mkdir ( "/run/sv/runit", 00755 ) ;
  (void) mkdir ( "/run/sv/s6", 00755 ) ;
  (void) mkdir ( "/run/sv/six", 00755 ) ;

  /* create the utmp database file /run/utmp */
  i = open ( "/run/utmp", O_WRONLY | O_TRUNC | O_CREAT | O_CLOEXEC, 00644 ) ;

  if ( 0 > i ) {
    perror ( "open() failed" ) ;
  } else {
#ifdef UTMP_GID
    if ( fchown ( i, 0, UTMP_GID ) ) {
      perror ( "fchown() failed" ) ;
      (void) fchmod ( i, 00644 ) ;
    } else if ( fchmod ( i, 00664 ) ) {
      perror ( "fchmod() failed" ) ;
    }
#endif

    (void) close_fd ( i ) ;
  }
}

static void seed_pseudofs ( void )
{
  /* "seed" the devfs mounted on /dev */
  seed_dev () ;
  /* "seed" the tmpfs mounted on /run */
  seed_run () ;
}

static void seed_misc ( void )
{
  int i ;

  (void) chmod ( "/", 00755 ) ;
  (void) mkdir ( "/tmp", 01777 ) ;
  (void) mkdir ( "/tmp/.ICE-unix", 01777 ) ;
  (void) mkdir ( "/tmp/.X11-unix", 01777 ) ;
  (void) mkdir ( "/var/tmp", 01777 ) ;
  (void) mkdir ( "/var/log", 00755 ) ;
  (void) chmod ( "/tmp", 01755 ) ;
  (void) chmod ( "/var/tmp", 01755 ) ;

  /* open7create the wtmp database file /var/log/wtmp */
  i = open ( "/var/log/wtmp", O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 00644 ) ;

  if ( 0 > i ) {
    perror ( "open() failed" ) ;
  } else {
#ifdef UTMP_GID
    if ( fchown ( i, 0, UTMP_GID ) ) {
      perror ( "fchown() failed" ) ;
      (void) fchmod ( i, 00644 ) ;
    } else if ( fchmod ( i, 00664 ) ) {
      perror ( "fchmod() failed" ) ;
    }
#endif

    (void) close_fd ( i ) ;
  }
}

static int dupfd2 ( const int o, const int n )
{
  if ( 0 <= o && 0 <= n ) {
    if ( o != n ) {
      int i ;

      do { i = dup2 ( o, n ) ; }
      while ( 0 > i && EINTR == errno ) ;

      return i ;
    }

    return n ;
  }

  return -3 ;
}

/* set terminal settings to reasonable defaults */
static void reset_term ( const int fd )
{
  if ( 0 <= fd && 0 != isatty ( fd ) ) {
    struct termios tio ;

    if ( tcgetattr ( fd, & tio ) ) { return ; }

    /* set control chars */
#if 0
    tio . c_cc [ VINTR ]	= 3	; /* C-c */
    tio . c_cc [ VQUIT ]	= 28	; /* C-\ */
    tio . c_cc [ VERASE ]	= 127	; /* C-? */
    tio . c_cc [ VKILL ]	= 21	; /* C-u */
    tio . c_cc [ VEOF ]		= 4	; /* C-d */
    tio . c_cc [ VSTART ]	= 17	; /* C-q */
    tio . c_cc [ VSTOP ]	= 19	; /* C-s */
    tio . c_cc [ VSUSP ]	= 26	; /* C-z */
#endif
    tio . c_cc [ VINTR ]	= CINTR ;
    tio . c_cc [ VQUIT ]	= CQUIT ;
    tio . c_cc [ VERASE ]	= CERASE ; /* ASCII DEL (0177) */
    tio . c_cc [ VKILL ]	= CKILL ;
    tio . c_cc [ VEOF ]		= CEOF ;
    tio . c_cc [ VTIME ]	= 0 ;
    tio . c_cc [ VMIN ]		= 1 ;
    tio . c_cc [ VSWTC ]	= _POSIX_VDISABLE ;
    tio . c_cc [ VSTART ]	= CSTART ;
    tio . c_cc [ VSTOP ]	= CSTOP ;
    tio . c_cc [ VSUSP ]	= CSUSP ;
    tio . c_cc [ VEOL ]		= _POSIX_VDISABLE ;
    tio . c_cc [ VREPRINT ]	= CREPRINT ;
    tio . c_cc [ VDISCARD ]	= CDISCARD ;
    tio . c_cc [ VWERASE ]	= CWERASE ;
    tio . c_cc [ VLNEXT ]	= CLNEXT ;
    tio . c_cc [ VEOL2 ]	= _POSIX_VDISABLE ;

#ifdef OSLinux
    /* use line discipline 0 */
    tio . c_line = 0 ;
#endif
#ifndef CRTSCTS
#  define CRTSCTS	0
#endif
    /* make it be sane */
    tio . c_cflag &= CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB
      | PARODD | CRTSCTS ;
    tio . c_cflag |= CREAD | HUPCL | CLOCAL ;
    tio . c_cflag &= CRTSCTS | PARODD | PARENB | CSTOPB | CSIZE | CBAUDEX | CBAUD ;
    tio . c_cflag |= CLOCAL | HUPCL | CREAD ;
    /* input modes */
    /* enable start/stop input and output control + map CR to NL on input */
    /* IGNPAR | IXANY */
    tio . c_iflag = ICRNL | IXON | IXOFF ;
#ifdef IUTF8
    tio . c_iflag |= IUTF8 ;
#endif
    /* output modes */
    /* Map NL to CR-NL on output */
    tio . c_oflag = ONLCR | OPOST ;
    /* local modes */
    /* ECHOPRT */
    tio . c_lflag = ECHO | ECHOCTL | ECHOE | ECHOK | ECHOKE |
      ICANON | IEXTEN | ISIG ;

    (void) tcsetattr ( fd, TCSANOW, & tio ) ;
    /* don't care about non-transmitted output data and non-read input data */
    (void) tcflush ( fd, TCIOFLUSH ) ;
  }
}

static int redirio ( const char * const path )
{
  if ( path && * path ) {
    const int fd = open ( path, O_RDWR | O_NONBLOCK | O_NOCTTY ) ;

    if ( 0 <= fd ) {
      int i = 0 ;

      i += ( 0 > dupfd2 ( fd, 0 ) ) ? -1 : 0 ;
      i += ( 0 > dupfd2 ( fd, 1 ) ) ? -1 : 0 ;
      i += ( 0 > dupfd2 ( fd, 2 ) ) ? -1 : 0 ;

      if ( isatty ( fd ) ) { reset_term ( fd ) ; }
      if ( 2 < fd ) { (void) close_fd ( fd ) ; }

      return i ;
    }
  }

  return -1 ;
}

/* redirect stdio to the console device */
static void consio ( void )
{
  char * s = getenv ( "CONSOLE" ) ;

  (void) seed_dev () ;

  if ( ! ( s && * s && is_c ( s ) ) ) { s = getenv ( "console" ) ; }

  if ( ! ( s && * s && is_c ( s ) ) ) {
    if ( is_c ( CONSOLE ) ) { s = CONSOLE ; }
#if defined (OSLinux)
    else if ( is_c ( VT_MASTER ) ) { s = VT_MASTER ; }
#endif
  }

  if ( s && * s && is_c ( s ) ) { (void) redirio ( s ) ; }
}

static void setup_io ( void )
{
  if ( isatty ( 0 ) && isatty ( 1 ) && isatty ( 2 ) ) {
    /* ok, already connected to a tty, use it for std io */
    reset_term ( 0 ) ;
  } else {
    /* redirect stdio to the console device */
    consio () ;
  }
}

#if 0
/* enable the loopback interface */
static void setup_iface_lo ( void )
{	
  const int fd = socket ( PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_IP ) ;

  if ( 0 <= fd ) {
    struct sockaddr_in ifaddr ;
    struct ifreq ipreq ;

    ifaddr . sin_family = AF_INET ;
    /* 127.0.0.1 */
    ifaddr . sin_addr = 16777343 ;
    ipreq . ifr_name = "lo" ;
    ipreq . ifr_addr = * ( (struct sockaddr *) & ifaddr ) ;
    (void) ioctl ( fd, SIOCSIFADDR, & ipreq ) ;
    ipreq . ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING ;
    (void) ioctl ( fd, SIOCSIFFLAGS, & ipreq ) ;
    (void) close_fd ( fd ) ;
  }
}
#endif

int main ( const int argc, char ** argv )
{
  int i = 0 ;
  unsigned long int f = 0 ;
  char * s = NULL ;
  char * ex = NULL ;
  char * pname = ( ( 0 < argc ) && argv && * argv && ** argv ) ?
    * argv : "inid" ;
  const uid_t myuid = getuid () ;
  const pid_t mypid = getpid () ;
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
  } else if ( 1 < mypid ) {
    (void) fprintf ( stderr, "\r%s:\tmust run as process #1\r\n", pname ) ;
    (void) fflush ( NULL ) ;
    return 100 ;
  }

  /* parse command line args */
  while ( 0 < ( i = getopt ( argc, argv, ":c:C:eEfFhHpPrRs:S:xX" ) ) ) {
    switch ( i ) {
      case 'e' :
      case 'E' :
        if ( optarg && * optarg && is_f ( optarg ) &&
          0 == access ( optarg, R_OK | X_OK ) )
        {
          ex = optarg ;
        }
        break ;
      case 's' :
      case 'S' :
        if ( optarg && * optarg ) {
          s = optarg ;
        }
        break ;
      case 'c' :
      case 'C' :
        if ( 1 == mypid ) {
          if ( optarg && * optarg && is_d ( optarg ) &&
            ( 0 == chdir ( optarg ) ) )
          {
            if ( chroot ( optarg ) ) { perror ( "chroot() failed" ) ; }
          }

          (void) chdir ( "/" ) ;
        }
        break ;
      default :
        break ;
    }
  }

  i = optind ;
  /* secure default file creation mask */
  (void) umask ( 00022 ) ;
  /* set process resource (upper) limits */
  (void) set_rlimits () ;
  /* set up default environment for subprocesses */
  setup_env () ;
  /* set a preliminary default host name */
  (void) chdir ( "/" ) ;
  (void) sethostname ( HOSTNAME, LEN( HOSTNAME ) - 1 ) ;
#ifdef DOMAIN
  (void) setdomainname ( DOMAIN, LEN( DOMAIN ) - 1 ) ;
#endif
  setup_io () ;
  (void) chvt ( 1 ) ;
  (void) setsid () ;
  (void) setpgid ( 0, 0 ) ;
#if defined (OSbsd)
  (void) setlogin ( "root" ) ;
#endif

  /* Ave imperator, morituri te salutant ! */
  (void) printf (
     "\r\n\r\n\r%s:\tpesi inid v%s booting ...\r\n\r\n\r\n"
     "\r%s:\tRunning the stage 1 tasks from %s now ...\r\n\r\n"
     , pname, PESO_VERSION, pname, STAGE1 ) ;
  /* mount pseudo fs */
  mount_pseudofs () ;
  seed_pseudofs () ;
  /*
  load_modules () ;
  setup_lo () ;
  */
  /* register pseudo signal handler for SIGCHLD and unblock everything */
  setup_sigs () ;
  /* run the stage 1 tasks now */
  (void) run_conf ( 0, STAGE1 ) ;
  (void) reap ( 1, 0 ) ;
  (void) chdir ( "/" ) ;
  consio () ;
  (void) chvt ( 1 ) ;
  (void) fflush ( NULL ) ;
  (void) reap ( 1, 0 ) ;
  /* ensure some important files/dirs exist */
  seed_misc () ;
  /* exec into the stage2 executable now */
  (void) printf ( "\r\n\r%s:\tExecing into stage 2 now ...\r\n", pname ) ;

  if ( ex && * ex && is_fnrx ( ex ) ) {
    (void) execve ( ex, argv, Env ) ;
    perror ( "execve() failed" ) ;
    (void) execvp ( ex, argv ) ;
    perror ( "execvp() failed" ) ;
  } else if ( ( argc > i ) && argv [ i ] && argv [ i ] [ 0 ] &&
    is_fnrx ( argv [ i ] ) )
  {
      (void) execve ( argv [ i ], i + argv, Env ) ;
      perror ( "execve() failed" ) ;
#if defined (__GLIBC__) && defined (_GNU_SOURCE)
      (void) execvpe ( argv [ i ], i + argv, Env ) ;
      perror ( "execvpe() failed" ) ;
#endif
      (void) execvp ( argv [ i ], i + argv ) ;
      perror ( "execvp() failed" ) ;
  }

  /* run rescue() and retry */
  (void) execve ( STAGE2X, argv, Env ) ;
  perror ( "execve() failed" ) ;
  (void) execvp ( "stage2", argv ) ;
  perror ( "execvp() failed" ) ;
  (void) fflush ( NULL ) ;

  return 111 ;
}

