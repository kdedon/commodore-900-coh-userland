#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>

/* /proc/loadavg's first field is "N.MM"; centiloads are its two decimals,
   so it is read as digits rather than through atof(). */
int getload(void)
{
  static int init=0, fd;
  char buf[10];
  int i, cent;

  if (!init)
  {
    fd=open("/proc/loadavg",O_RDONLY);
    if (fd<0) { perror("Can't open /proc/loadavg"); exit(1); }
    init=1;
  }
  lseek(fd,(off_t)0,SEEK_SET);
  read(fd,buf,sizeof(buf)); buf[sizeof(buf)-1]='\0';
  for (cent=0, i=0; buf[i]>='0' && buf[i]<='9'; i++)
    cent=cent*10+(buf[i]-'0');
  cent*=100;
  if (buf[i]=='.')
  {
    if (buf[i+1]>='0' && buf[i+1]<='9') cent+=10*(buf[i+1]-'0');
    if (buf[i+2]>='0' && buf[i+2]<='9') cent+=buf[i+2]-'0';
  }
  return cent;
}
