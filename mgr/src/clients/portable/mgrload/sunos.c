/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int getload(void)
{
  FILE *w;
  char str[80], *s;

  while (1)
  {
    if ((w=popen("/usr/kvm/w","r"))==(FILE*)0)
    {
      fprintf(stderr,"Can't open pipe to /usr/kvm/w\n");
      exit(1);
    }
    else
    {
      fgets(str,sizeof(str),w);
      pclose(w);
      return((int)(atof(strrchr(str,':')+2)*100.0));
    }
  }
}
