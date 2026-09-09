/*{{{}}}*/
/*{{{  Notes*/
/*

wtmp support is still missing, and most probably bugs are lurking in the
dark.

Usage: mgrlogin [-b background-bitmap][-f default-font] tty

`tty' is a name under /dev, e.g. `console'.  The named terminal becomes the
controlling terminal and stdin/stdout/stderr; ESC at the login prompt hands
it to /etc/getty for an ordinary character login.

Michael

*/
/*}}}  */
/*{{{  #includes*/
#define _POSIX_SOURCE
#include <termios.h>
#include <sys/stat.h>
#include <limits.h>
#include <getopt.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <grp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <netdb.h>
#include <sysexits.h>
#include <mgr/bitblit.h>
#include <mgr/font.h>
#include <mgr/window.h>

#include "proto.h"
#include "font_subs.h"
#include "set_mode.h"
/*}}}  */
/*{{{  #defines*/
/* Where mail(1) delivers on this system -- cmd/mail/mail.h's SPOOLDIR. */
#define MAILDIR "/usr/spool/mail"
/* login(1)'s two search paths, each with the window clients' directory added. */
#define PATH_ROOT "/bin:/usr/bin:/etc:/usr/mgr/bin:"
#define PATH_USER ":/bin:/usr/bin:/usr/mgr/bin"
#define LOGFILE "/usr/adm/mgrlogin.log"
#define GETTY "/etc/getty"
#define GETTYSPEED "P"
/* Length of an encrypted password, as login(1) and su(1) both keep it. */
#define PASSLEN 13
#ifndef _NSIG
#ifdef NSIG
#define _NSIG NSIG
#else
#define _NSIG 32
#endif
#endif

#ifndef _POSIX_PATH_MAX
#define _POSIX_PATH_MAX 255
#endif
/*}}}  */

/*{{{  variables*/
struct font *font;
BITMAP *screen;

/* Where the program has got to, named in every line it logs. */
char *stage = "startup";

/* Whether set_tty(0) has changed the terminal modes, so that restore() does
   not put back modes that were never saved. */
int tty_raw = 0;

#ifdef DEBUG
int debug = 0;
char *debug_level = "";
#endif
/*}}}  */

/*{{{  numstr*/
/*
 * Write the decimal text of `n' at `buf' and return the terminating null, so
 * that the caller can go on appending.  Hand-rolled because the signal
 * handler below formats a number without calling into stdio.
 */
char *numstr(buf, n)
char *buf;
int n;
{
  char digits[8];
  int i;

  if (n<0) { *buf++='-'; n= -n; }
  i=0;
  do { digits[i++]='0'+(n%10); n/=10; } while (n!=0 && i<8);
  while (i) *buf++=digits[--i];
  *buf='\0';
  return buf;
}
/*}}}  */
/*{{{  report*/
/*
 * Report a failure on the standard error and append the same line to
 * LOGFILE.  Both, because the standard error is the terminal being managed
 * and may be in graphics mode, in which case the write leaves no readable
 * trace; the log file is where the record survives.  The line names the
 * stage the program had reached, and `num' is appended when `hasnum' is set.
 *
 * Only string primitives and one write(2) per destination, so that a signal
 * handler can report through it.
 */
void report(what, why, num, hasnum)
char *what;
char *why;
int num;
int hasnum;
{
  int fd;
  int n;
  char msg[256];

  strcpy(msg,"mgrlogin: ");
  strcat(msg,stage);
  strcat(msg,": ");
  strcat(msg,what);
  strcat(msg,": ");
  strcat(msg,why);
  if (hasnum) { strcat(msg," "); numstr(msg+strlen(msg),num); }
  strcat(msg,"\n");
  n=strlen(msg);
  /* The standard error is the terminal being managed.  While the bitmap is up
     a write to it is drawn over by the next thing painted and scribbles on
     what is already there, so it goes only to the log; once restore() has put
     the screen back it is the one place a message can be read at once. */
  if (screen==(BITMAP*)0) write(2,msg,n);
  if ((fd=open(LOGFILE,O_WRONLY|O_APPEND|O_CREAT,0644))>=0)
  {
    write(fd,msg,n);
    close(fd);
  }
}
/*}}}  */
/*{{{  complain*/
void complain(what, why)
char *what;
char *why;
{
  report(what,why,0,0);
}
/*}}}  */
/*{{{  restore*/
/*
 * Leave the terminal in the state an ordinary program can use: the bitmap
 * released, the frame buffer back in text mode and the modes set_tty(0)
 * changed put back.  Does nothing for a screen that was never opened, and
 * may be called twice.
 */
void restore()
{
  if (screen!=(BITMAP*)0)
  {
    bit_destroy(screen);
    screen=(BITMAP*)0;
    bit_textscreen();
  }
  if (tty_raw) { reset_tty(0); tty_raw=0; }
}
/*}}}  */
/*{{{  died*/
/*
 * A signal arriving while the login box is up would end this process with the
 * frame buffer still in graphics mode, leaving nothing on the screen to read
 * and nothing on disk to look at.  Put the screen back, name the signal and
 * the stage in LOGFILE, and go.
 */
void died(sig)
int sig;
{
  signal(sig,SIG_DFL);
  restore();
  report("signal","terminated by signal",sig,1);
  exit(EX_SOFTWARE);
}
/*}}}  */
/*{{{  printcursor*/
void printcursor(x, y, on)
int x;
int y;
int on;
{
  bit_blit(screen,x,y-font->head.high,font->head.wide,font->head.high,on ? BIT_SET : BIT_CLR,(BITMAP*)0,0,0);
}
/*}}}  */
/*{{{  printchar*/
void printchar(x, y, c)
int x;
int y;
unsigned char c;
{
  bit_blit(screen,x,y-font->head.high,font->head.wide,font->head.high,BIT_SRC,font->glyph[c],0,0);
}
/*}}}  */
/*{{{  printstr*/
void printstr(x, y, s)
int x;
int y;
char *s;
{
  while (*s) { printchar(x,y,*s++); x+=font->head.wide; }
}
/*}}}  */
/*{{{  edit*/
unsigned char edit(x, y, s, visible)
int x;
int y;
char *s;
int visible;
{
  unsigned char c;
  int len;
  int n;

  len=strlen(s);
  while (1)
  {
    printcursor(x,y,1);
    /* A terminal that has gone away returns 0 or -1 for ever; without this
       the loop spins on a stale character and nothing can reclaim the
       screen.  A signal that interrupts the read leaves the terminal
       perfectly usable, so that case goes round again. */
    n=read(0,&c,1);
    if (n!=1)
    {
      if (n<0 && errno==EINTR) continue;
      restore();
      if (n==0) report("stdin","end of input on the controlling terminal",0,0);
      else report("stdin","read failed, errno",errno,1);
      exit(EX_IOERR);
    }
    printcursor(x,y,0);
    switch (c)
    {
      /*{{{  escape*/
      case 27: return c;
      /*}}}  */
      /*{{{  return*/
      case '\r':
      case '\n': return c;
      /*}}}  */
      /*{{{  backspace*/
      case 127:
      case 8:
      {
        if (len) { s[--len]='\0'; if (visible) x-=font->head.wide; }
        break;
      }
      /*}}}  */
      /*{{{  default*/
      default:
      {
        if (len<8)
        {
          s[len++]=c;
          s[len]='\0';
          if (visible) { printchar(x,y,c); x+=font->head.wide; }
        }
      }
      /*}}}  */
    }
  }
}
/*}}}  */
/*{{{  cutebox*/
void cutebox(bx, by, bw, bh)
int bx;
int by;
int bw;
int bh;
{
  bit_blit(screen,bx,by,bw,bh,BIT_CLR,(BITMAP*)0,0,0);

  bit_line(screen,bx,by,bx+bw,by,BIT_SRC);
  bit_line(screen,bx+bw,by,bx+bw,by+bh,BIT_SRC);
  bit_line(screen,bx+bw,by+bh,bx,by+bh,BIT_SRC);
  bit_line(screen,bx,by+bh,bx,by,BIT_SRC);

  bit_line(screen,bx+1,by+1,bx+bw-1,by+1,BIT_SRC);
  bit_line(screen,bx+bw-1,by+1,bx+bw-1,by+bh-1,BIT_SRC);
  bit_line(screen,bx+bw-1,by+bh-1,bx+1,by+bh-1,BIT_SRC);
  bit_line(screen,bx+1,by+bh-1,bx+1,by+1,BIT_SRC);

  bit_line(screen,bx+3,by+3,bx+bw-3,by+3,BIT_SRC);
  bit_line(screen,bx+bw-3,by+3,bx+bw-3,by+bh-3,BIT_SRC);
  bit_line(screen,bx+bw-3,by+bh-3,bx+3,by+bh-3,BIT_SRC);
  bit_line(screen,bx+3,by+bh-3,bx+3,by+3,BIT_SRC);
}
/*}}}  */

/*{{{  main*/
int main(argc, argv)
int argc;
char *argv[];
{
  /*{{{  variables*/
  int x,login_y,password_y,sig;
  unsigned char loginstr[9],passwordstr[9],ret;
  char ttystr[_POSIX_PATH_MAX];
  char *background=(char*)0;
  char *fontname=(char*)0;
  /*}}}  */

  /*{{{  parse arguments*/
  {
    int c;

    while ((c=getopt(argc,argv,"b:f:"))!=EOF) switch (c)
    {
      case 'b': background=optarg; break;
      case 'f': fontname=optarg; break;
    }
    /*{{{  parse tty*/
    {
      int tty;

      if (optind>=argc)
      {
        fprintf(stderr,"Usage: %s [-b background][-f font] tty\n",argv[0]);
        exit(1);
      }
      strcpy(ttystr,"/dev/");
      strcat(ttystr,argv[optind]);

      /* Become a process-group leader before the open, so that the open
         claims the line as this process' controlling terminal: sys/drv/tty.c
         does that only for a process whose group equals its own pid.  There
         is no setsid() in this kernel. */
      setpgrp();

      /* The open happens while the inherited standard error is still in
         place, so a terminal that cannot be opened is reported to whoever
         started this rather than into the descriptors just closed. */
      if ((tty=open(ttystr,O_RDWR))<0)
      {
        complain(ttystr,"cannot open controlling terminal");
        exit(1);
      }
      fchmod(tty,0600);
      fchown(tty,getuid(),getgid());

      /* The terminal becomes all three standard descriptors.  dup(2) hands
         back the lowest free one, so closing in this order fills 0, then 1,
         then 2. */
      if (tty!=0) { close(0); if (dup(tty)!=0) exit(1); close(tty); }
      close(1); dup(0);
      close(2); dup(0);
    }
    /*}}}  */
  }
  /*}}}  */
  /*{{{  get into grafics mode*/
  /* Everything that can be caught goes to died(), because from here until the
     exec the screen is graphics and an uncaught signal would end the process
     leaving neither a readable screen nor a record.  SIGKILL cannot be
     caught; numbers this kernel does not have fail harmlessly. */
  for (sig=1; sig<_NSIG; sig++) if (sig!=SIGKILL) signal(sig,died);
  stage="opening the display";
  set_tty(0);
  tty_raw=1;
  if ((screen=bit_open(SCREEN_DEV))==(BITMAP*)0)
  {
    restore();
    complain(SCREEN_DEV,"cannot open the display");
    exit(EX_NOPERM);
  }
  bit_grafscreen();
  /*}}}  */
  /*{{{  load font*/
  stage="loading the font";
  if (fontname)
  {
    char fontpath[_POSIX_PATH_MAX];

    if (*fontname=='/' || *fontname=='.') strcpy(fontpath,fontname);
    else { strcpy(fontpath,FONTDIR); strcat(fontpath,"/"); strcat(fontpath,fontname); }

    if ((font=open_font(fontpath))==(struct font*)0)
    {
      complain(fontpath,"cannot load font, using the compiled-in one");
      font=open_font((char*)0);
    }
  }
  else font=open_font((char*)0);
  if (font==(struct font*)0)
  {
    restore();
    complain("font","no font at all, not even the compiled-in one");
    exit(EX_OSFILE);
  }
  /*}}}  */
  /*{{{  draw background*/
  stage="drawing the background";
  bit_blit(screen,0,0,screen->wide,screen->high,BIT_CLR,(BITMAP*)0,0,0);
  if (background)
  {
    BITMAP *bp;
    FILE *fp;
    char backgroundpath[_POSIX_PATH_MAX];

    if (*background=='/' || *background=='.') strcpy(backgroundpath,background);
    else { strcpy(backgroundpath,ICONDIR); strcat(backgroundpath,"/"); strcat(backgroundpath,background); }

    if ((fp=fopen(backgroundpath,"r"))!=(FILE*)0 && (bp=bitmapread(fp))!=(BITMAP*)0)
    {
      int x,y;

      for (x=0; x<screen->wide; x+=bp->wide) bit_blit
      (
        screen,
        x,0,
        screen->wide-x<bp->wide ? screen->wide-x : bp->wide,
        bp->high,
        BIT_SRC,bp,0,0
      );

      for (y=0; y<screen->high; y+=bp->high) bit_blit
      (
        screen,
        0,y,
        screen->wide,
        screen->high-y<bp->high ? screen->high-y : bp->high,
        BIT_SRC,screen,0,0
      );
    }
  }
  /*}}}  */
  /*{{{  draw hostname*/
  stage="drawing the hostname";
  {
    int bx,bw,by,bh;
    char hostname[_POSIX_PATH_MAX];
    struct hostent *h;

    gethostname(hostname,sizeof(hostname));
    if ((h=gethostbyname(hostname))!=(struct hostent*)0) strcpy(hostname,h->h_name);
    bw=font->head.wide*(strlen(hostname)+2);
    bh=2*font->head.high;
    bx=(screen->wide-bw)/2;
    by=screen->high/6-bh/2;
    cutebox(bx,by,bw,bh);
    printstr(bx+font->head.wide,by+bh-font->head.high/2,hostname);
  }
  /*}}}  */
  /*{{{  draw login box*/
  {
    int bx,bw,by,bh;

    bx=(screen->wide-font->head.wide*40)/2;
    by=(screen->high-font->head.high*8)/2;
    bw=font->head.wide*40;
    bh=font->head.high*8;
    cutebox(bx,by,bw,bh);
  }
  /*}}}  */
  /*{{{  draw login box contents*/
  x=(screen->wide-font->head.wide*18)/2;
  login_y=screen->high/2-font->head.high/6;
  password_y=screen->high/2+font->head.high/6+font->head.high;
  printstr(x,password_y,"Password:         ");
  printstr((screen->wide-font->head.wide*28)/2,login_y-2*font->head.high,
  "Press ESC for terminal login");
  stage="waiting at the login box";
  report("box","drawn, waiting for input",0,0);
  /*}}}  */
  while (1)
  {
    /*{{{  get login and password or escape*/
    printstr(x,login_y,"Login:            ");
    *loginstr='\0'; *passwordstr='\0';
    do
    {
      ret=edit(x+font->head.wide*10,login_y,loginstr,1);
    } while ((ret=='\r' || ret=='\n') && *loginstr=='\0');
    if (ret=='\r' || ret=='\n')
    {
      ret=edit(x+font->head.wide*10,password_y,passwordstr,0);
      if (ret=='\r' || ret=='\n');
    }
    /*}}}  */
    if (ret==27)
    /*{{{  hand the terminal to getty for a character login*/
    {
      stage="handing the terminal to getty";
      restore();
      /* getty takes the speed table letter as argv[1]; argv[0] is the line
         type, `-' for a local line, exactly as init(1M) spawns it. */
      execl(GETTY,"-",GETTYSPEED,(char*)0);
      complain(GETTY,"cannot execute");
      exit(EX_OSFILE);
    }
    /*}}}  */
    else
    /*{{{  try login*/
    {
      struct passwd *pw;
      int ok;

      stage="authenticating";
      /* The same policy as login(1) (cmd/login/login.c:272-288): an account
         with no password in the file is entered without one, an empty answer
         never matches an account that has one, and crypt(3) is run even for a
         name that does not exist so that the two rejections take the same
         time.  crypt(3) reads two salt characters, which an empty pw_passwd
         does not have.

         A field that is neither empty nor a whole encrypted password is not
         a password and cannot be entered, which is why the length is checked
         as well as the comparison: crypt(3) copies the two salt characters it
         was handed to the front of its answer, so a ONE-character field makes
         crypt(3) copy that character and then the terminating NUL, and the
         answer it returns is that same one-character string -- it compares
         equal to the field for ANY password typed here.  login(1) (login.c)
         and su(1) both require strlen(pw_passwd)==PASSLEN for this reason. */
      pw=getpwnam(loginstr);
      ok=0;
      if (pw!=(struct passwd*)0 && pw->pw_passwd[0]=='\0') ok=1;
      else if (passwordstr[0]!='\0')
      {
        char *enc;

        enc=crypt((char*)passwordstr,pw==(struct passwd*)0 ? "xx" : pw->pw_passwd);
        if (pw!=(struct passwd*)0 && strcmp(enc,pw->pw_passwd)==0
            && strlen(pw->pw_passwd)==PASSLEN) ok=1;
      }
      if (ok)
      /*{{{  start window manager*/
      {
        char mgrlogin[_POSIX_PATH_MAX];
        char env_user[_POSIX_PATH_MAX],env_logname[_POSIX_PATH_MAX];
        char env_home[_POSIX_PATH_MAX],env_shell[_POSIX_PATH_MAX];
        char env_path[_POSIX_PATH_MAX],env_mail[_POSIX_PATH_MAX];
        char *login_env[7];
        char *login_argv[2];
        int i;

        /* Assigned rather than initialised: these are automatic arrays, and
           an initialiser on an automatic aggregate is outside K&R C. */
        login_env[0]=env_user; login_env[1]=env_logname; login_env[2]=env_home;
        login_env[3]=env_shell; login_env[4]=env_path; login_env[5]=env_mail;
        login_env[6]=(char*)0;
        login_argv[0]="mgr"; login_argv[1]=(char*)0;

        sprintf(env_user,"USER=%s",pw->pw_name);
        sprintf(env_logname,"LOGNAME=%s",pw->pw_name);
        sprintf(env_home,"HOME=%s",pw->pw_dir);
        sprintf(env_shell,"SHELL=%s",pw->pw_shell==(char*)0 || pw->pw_shell[0]=='\0' ? "/bin/sh" : pw->pw_shell);
        sprintf(env_path,"PATH=%s",pw->pw_uid==0 ? PATH_ROOT : PATH_USER);
        sprintf(env_mail,"MAIL=%s/%s",MAILDIR,pw->pw_name);
        if (chdir(pw->pw_dir)!=0) chdir("/");
        if (ttyname(0)) { chown(ttyname(0),pw->pw_uid,pw->pw_gid); chmod(ttyname(0),0600); }
        for (i=1; i<=_NSIG; i++) signal(i,SIG_DFL);
        stage="starting the window server";
        report(MGR_BINARY,"authenticated, execing",0,0);
        restore();
        initgroups(pw->pw_name,pw->pw_gid);
        setgid(pw->pw_gid);
        setuid(pw->pw_uid);
        sprintf(mgrlogin,"%s/.mgrlogin",pw->pw_dir);
        execve(mgrlogin,login_argv,login_env);
        execve(MGR_BINARY,login_argv,login_env);
        complain(MGR_BINARY,"cannot execute the window server");
        exit(EX_OSFILE);
      }
      /*}}}  */
      else
      /*{{{  incorrect login*/
      {
        printstr((screen->wide-font->head.wide*16)/2,login_y+3*font->head.high,
        "Login incorrect");
      }
      /*}}}  */
    }
    /*}}}  */
  }
}
/*}}}  */
