#include "sim.h"


/* Sound routines */


int SoundInitialized = 0;
short Dozing;


int InitializeSound()
{
  char cmd[256];

  SoundInitialized = 1;

  if (!UserSoundOn) return;

  Eval("UIInitializeSound");
}


int ShutDownSound()
{
  if (SoundInitialized) {
    SoundInitialized = 0;
    Eval("UIShutDownSound");
  }
}


int MakeSound(channel, id)
char *channel;
char *id;
{
  char buf[256];

  if (!UserSoundOn) return;
  if (!SoundInitialized) InitializeSound();

  sprintf(buf, "UIMakeSound \"%s\" \"%s\"", channel, id);
  Eval(buf);
}


int MakeSoundOn(view, channel, id)
SimView *view;
char *channel;
char *id;
{
  /* ncurses port: sound is not per-window in a terminal, so a view-specific
   * sound just plays on the global channel (the old code routed it through the
   * view's Tk widget path via Tk_PathName).
   */
  MakeSound(channel, id);
}


int StartBulldozer()
{
  if (!UserSoundOn) return;
  if (!SoundInitialized) InitializeSound();
  if (!Dozing) {
    DoStartSound("edit", "1");
    Dozing = 1;
  }
}


int StopBulldozer()
{
  if ((!UserSoundOn) || (!SoundInitialized)) return;
  DoStopSound("1");
  Dozing = 0;
}


/* comefrom: doKeyEvent */
int SoundOff()
{
  if (!SoundInitialized) InitializeSound();
  Eval("UISoundOff");
  Dozing = 0;
}


int DoStartSound(channel, id)
char *channel;
char *id;
{
  char buf[256];

  sprintf(buf, "UIStartSound %s %s", channel, id);
  Eval(buf);
}


int DoStopSound(id)
char *id;
{
  char buf[256];

  sprintf(buf, "UIStopSound %s", id);
  Eval(buf);
}
