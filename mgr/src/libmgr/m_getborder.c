/*
 * Portions Copyright (c) 2026 Kevin Dedon.
 */
#include <mgr/mgr.h>
#include <ctype.h>
#include <stdio.h>

int m_getbordersize()
{
  int n,dummy;
  char *p=m_linebuf;

  _m_ttyset();
  m_getinfo(G_SYSTEM);
  m_gets(m_linebuf);
  _m_ttyreset();
  while (*p && *p == ' ') p++;
  while (*p && *p != ' ') p++;	/* skip hostname */
  return (sscanf(p,"%d %d %d",&dummy,&dummy,&n)==3 ? n : -1);
}

int m_dflbordersize()
{
  int n,dummy;
  char *p=m_linebuf;

  _m_ttyset();
  m_getinfo(G_SYSTEM);
  m_gets(m_linebuf);
  _m_ttyreset();
  while (*p && *p == ' ') p++;
  while (*p && *p != ' ') p++;	/* skip hostname */
  return (sscanf(p,"%d %d %d",&dummy,&dummy,&n)==3 ? n : -1);
}
