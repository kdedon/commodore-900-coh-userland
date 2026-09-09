#include "sim.h"
#include <string.h>

/* Resources live on disk, not in the binary.  The upstream port bakes res/ and
 * cities/ into res_data.h -- 870 KB of `static const unsigned char' -- which no
 * amount of segment arithmetic makes fit: static data must live in ONE 64K
 * hardware segment on this machine, and no single object may cross a segment
 * boundary at all.  So res_find()/city_find() keep their signature and read the
 * named file out of RESDIR instead.
 *
 * Both are documented as returning a pointer the caller must not modify and
 * must use before the next call: GetResource() copies immediately, and
 * LoadEmbeddedCity() consumes immediately, so one buffer serves both and the
 * largest resource (a 27,120-byte city) sizes it.
 */

#define RESDIR	"/usr/games/lib/ttycity"

char *HomeDir, *ResourceDir, *KeyDir, *HostName;

struct Resource *Resources = NULL;

static char *ResBuf = NULL;		/* the one shared read buffer */
static unsigned int ResBufLen = 0;

/* Read RESDIR/<name> (or $SIMRES/<name>) whole.  NULL if absent or unreadable. */
static const unsigned char *
res_read(name, size)
const char *name;
unsigned int *size;
{
  char path[256];
  char *dir;
  FILE *f;
  long len;
  unsigned int n;

  if (size) *size = 0;
  dir = ResourceDir;
  if ((dir == NULL) || (*dir == '\0'))
    dir = RESDIR;
  if ((strlen(dir) + strlen(name) + 2) > sizeof(path))
    return ((const unsigned char *)0);
  sprintf(path, "%s/%s", dir, name);

  if ((f = fopen(path, "r")) == NULL)
    return ((const unsigned char *)0);
  fseek(f, 0L, 2);
  len = ftell(f);
  fseek(f, 0L, 0);
  if ((len <= 0L) || (len > 65000L)) {		/* one object, one segment */
    fclose(f);
    return ((const unsigned char *)0);
  }
  n = (unsigned int)len;

  if (n > ResBufLen) {
    if (ResBuf != NULL) ckfree(ResBuf);
    ResBuf = (char *)ckalloc(n);
    ResBufLen = (ResBuf == NULL) ? 0 : n;
  }
  if (ResBuf == NULL) {
    fclose(f);
    return ((const unsigned char *)0);
  }
  if (fread(ResBuf, 1, (int)n, f) != n) {
    fclose(f);
    return ((const unsigned char *)0);
  }
  fclose(f);
  if (size) *size = n;
  return ((const unsigned char *)ResBuf);
}

/* Find a resource by exact basename ("stri.301", "snro.111"). */
const unsigned char *
res_find(name, size)
const char *name;
unsigned int *size;
{
  return res_read(name, size);
}

/* Find a bundled example city by exact basename ("about.cty"). */
const unsigned char *
city_find(name, size)
const char *name;
unsigned int *size;
{
  char path[64];

  if ((strlen(name) + 8) > sizeof(path)) {
    if (size) *size = 0;
    return ((const unsigned char *)0);
  }
  sprintf(path, "cities/%s", name);
  return res_read(path, size);
}

/* The bundled cities, for the built-in load picker.  There is no opendir() in
 * this libc, so the shipped set is named here rather than scanned; a city the
 * install left out simply does not open, which the picker already handles. */
static char *CityNames[] = {
  "about.cty",	  "badnews.cty",  "bluebird.cty", "bruce.cty",
  "deadwood.cty", "finnigan.cty", "freds.cty",	  "haight.cty",
  "happisle.cty", "joffburg.cty", "kamakura.cty", "kobe.cty",
  "kowloon.cty",  "kyoto.cty",	  "linecity.cty", "med_isle.cty",
  "ndulls.cty",	  "neatmap.cty",  "radial.cty",	  "senri.cty",
  "southpac.cty", "splats.cty",	  "wetcity.cty",  "yokohama.cty"
};
#define CITY_COUNT ((int)(sizeof(CityNames) / sizeof(CityNames[0])))

int
EmbeddedCityCount()
{
  return CITY_COUNT;
}

const char *
EmbeddedCityName(i)
int i;
{
  return (i >= 0 && i < CITY_COUNT) ? (const char *)CityNames[i]
				    : (const char *)0;
}

struct StringTable {
  QUAD id;
  int lines;
  char **strings;
  struct StringTable *next;
} *StringTables;


Handle GetResource(name, id)
char *name;
QUAD id;
{
  struct Resource *r = Resources;
  char key[16];
  const unsigned char *data;
  unsigned int size;

  while (r != NULL) {
    if ((r->id == id) &&
	(strncmp(r->name, name, 4) == 0)) {
      return ((Handle)&r->buf);
    }
    r = r->next;
  }

  /* resources are baked into the binary (res_data.h); look up by basename */
  sprintf(key, "%c%c%c%c.%d", name[0], name[1], name[2], name[3], (int)id);
  data = res_find(key, &size);
  if ((data == NULL) || (size == 0))
    return (NULL);		/* missing: caller (GetIndString) degrades */

  r = (struct Resource *)ckalloc(sizeof(struct Resource));
  r->name[0] = name[0];
  r->name[1] = name[1];
  r->name[2] = name[2];
  r->name[3] = name[3];
  r->id = id;
  r->size = size;

  /* hand back a MUTABLE copy: GetIndString rewrites '\n' -> '\0' in place,
   * so we must never expose the const baked-in bytes directly. */
  r->buf = (char *)ckalloc(size);
  memcpy(r->buf, data, size);
  r->next = Resources; Resources = r;
  return ((Handle)&r->buf);
}


void
ReleaseResource(r)
Handle r;
{
}


QUAD
ResourceSize(h)
Handle h;
{
  struct Resource *r = (struct Resource *)h;

  return (r->size);
}


char *
ResourceName(h)
Handle h;
{
  struct Resource *r = (struct Resource *)h;

  return (r->name);
}


QUAD
ResourceID(h)
Handle h;
{
  struct Resource *r = (struct Resource *)h;

  return (r->id);
}


int GetIndString(str, id, num)
char *str;
int id;
short num;
{
  struct StringTable **tp, *st = NULL;
  Handle h;

  tp = &StringTables;

  while (*tp) {
    if ((*tp)->id == id) {
      st = *tp;
      break;
    }
    tp = &((*tp)->next);
  }
  if (!st) {
    QUAD i, lines, size;
    char *buf;

    st = (struct StringTable *)ckalloc(sizeof (struct StringTable));
    st->id = id;
    h = GetResource("stri", (QUAD)id);
    if (h == NULL) {		/* resource file missing: degrade, don't crash */
      ckfree((char *)st);
      strcpy(str, "");
      return;
    }
    size = ResourceSize(h);
    buf = (char *)*h;
    for (i=0, lines=0; i<size; i++)
      if (buf[i] == '\n') {
	buf[i] = 0;
	lines++;
      }
    st->lines = lines;
    st->strings = (char **)ckalloc(size * sizeof(char *));
    for (i=0; i<lines; i++) {
      st->strings[i] = buf;
      buf += strlen(buf) + 1;
    }
    st->next = StringTables;
    StringTables = st;
  }
  if ((num < 1) || (num > st->lines)) {
    strcpy(str, "");		/* ncurses port: silent (was a stderr print) */
  } {
    strcpy(str, st->strings[num-1]);
  }
}
