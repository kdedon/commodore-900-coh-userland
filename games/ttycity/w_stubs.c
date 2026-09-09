#include "sim.h"


/* Stubs */


QUAD TotalFunds;
short PunishCnt;
short autoBulldoze, autoBudget;
QUAD LastMesTime;
short GameLevel;
short InitSimLoad;
short ScenarioID;
short SimSpeed;
short SimMetaSpeed;
short UserSoundOn;
char *CityName;
short NoDisasters;
short MesNum;
short EvalChanged;
short flagBlink;


/* The treasury is a QUAD, so both of these take one.  There are no prototypes:
 * every caller must hand over a QUAD-typed expression, or it pushes two bytes
 * where four are read. */
int Spend(dollars)
QUAD dollars;
{
  SetFunds(TotalFunds - dollars);
}


int SetFunds(dollars)
QUAD dollars;
{
  TotalFunds = dollars;
  UpdateFunds();
}


/* Mac */

QUAD TickCount()
{
  struct timeval time;

  gettimeofday(&time, (char *)0);

  return (QUAD)((time.tv_sec / 60) + (time.tv_usec * 1000000 / 60));
}


Ptr
NewPtr(size)
int size;
{
  return ((Ptr)calloc(size, sizeof(Byte)));
}


/* w_hlhandlers.c */

int GameStarted()
{
  InvalidateMaps();
  InvalidateEditors();
  gettimeofday(&start_time, NULL);

  switch (Startup) {
  case -2: /* Load a city */
    if (LoadCity(StartupName)) {
      DoStartLoad();
      StartupName = NULL;
      break;
    }
    StartupName = NULL;
  case -1:
    if (StartupName != NULL) {
      setCityName(StartupName);
      StartupName = NULL;
    } else {
      setCityName("NowHere");
    }
    DoPlayNewCity();
    break;
  case 0:
    DoReallyStartGame();
    break;
  default: /* scenario number */
    DoStartScenario(Startup);
    break;
  }
}


int DoPlayNewCity()
{
  Eval("UIPlayNewCity");
}


int DoReallyStartGame()
{
  Eval("UIReallyStartGame");
}


int DoStartLoad()
{
  Eval("UIStartLoad");
}


int DoStartScenario(scenario)
int scenario;
{
  char buf[256];

  sprintf(buf, "UIStartScenario %d", scenario);
  Eval(buf);
}


int DropFireBombs()
{
  Eval("DropFireBombs");
}


int InitGame()
{
  sim_skips = sim_skip = sim_paused = sim_paused_speed = heat_steps = 0;
  setSpeed(0);
}


int ReallyQuit()
{
  sim_exit(0); /* Just sets tkMustExit and ExitReturn */
}


