/* nc_stubs.c -- UI-callback stubs.
 *
 * The simulation engine calls into a small set of UI functions from the
 * discarded X11/Tcl files (w_x.c, w_tk.c, w_editor.c, w_map.c, ...).  Each
 * stub here is a no-op or minimal; real curses implementations live in
 * nc_render.c, nc_minimap.c, ... and any still stubbed here is unported.
 */

#include "sim.h"

/* ---- globals owned by not-yet-ported files (w_graph.c / w_tool.c /
 *      w_sprite.c) that kept engine code references ------------------------- */

short Graph10Max, Graph120Max;		/* w_graph.c */
short NewGraph;				/* w_graph.c */
int UpdateDelayed;			/* w_update/w_tool */
/* OverRide / PendingTool are defined by w_tool.c; CrashX/CrashY by w_sprite.c */

/* ---- lifecycle / run-loop flags (were in w_tk.c / sim.c) ------------------ */

int tkMustExit = 0;

void Kick()
{ }
void UpdateFlush()
{ }
void DoTimeoutListen()
{ }
void DoStopMicropolis()
{ }
void StopToolkit()
{ }
/* ResetLastKeys is provided by kept w_keys.c */

/* ---- redraw / invalidate (were in w_tk.c / w_x.c) ------------------------- */

void InvalidateMaps()
{ }
void InvalidateEditors()
{ }
void RedrawMaps()
{ }
void RedrawEditors()
{ }
void EventuallyRedrawView(view)
SimView *view;
{ }
void CancelRedrawView(view)
SimView *view;
{ }
/* DoUpdateEditor is provided by nc_render.c */
int  DoUpdateMap(view)
SimView *view;
{ return 0; }
void DoNewEditor(view)
SimView *view;
{ }
void DoNewMap(view)
SimView *view;
{ }
void ViewToTileCoords(view, x, y, tx, ty)
SimView *view;
int x;
int y;
int *tx;
int *ty;
{ *tx = 0; *ty = 0; }
void ViewToPixelCoords(view, x, y, px, py)
SimView *view;
int x;
int y;
int *px;
int *py;
{ *px = 0; *py = 0; }
void DidStopPan(view)
SimView *view;
{ }

/* ---- allocators (were ckalloc in w_x.c) ---------------------------------- */

Sim *MakeNewSim()
{
  return (Sim *)calloc(1, sizeof(Sim));
}

SimView *MakeNewView()
{
  return (SimView *)calloc(1, sizeof(SimView));
}

/* ---- timer / earthquake (were Tk timers in w_tk.c) ----------------------- */

void StartMicropolisTimer()
{ }
void StopMicropolisTimer()
{ }
void FixMicropolisTimer()
{ }
/* DoEarthQuake / StopEarthquake are provided by nc_render.c (screen shake) */

/* ---- chalk / ink overlay (discarded feature) ----------------------------- */

Ink *NewInk()
{ return (Ink *)0; }
void FreeInk(ink)
Ink *ink;
{ }
void StartInk(x, y)
int x;
int y;
{ }
void AddInk(x, y)
int x;
int y;
{ }
void EraseOverlay()
{ }

/* ---- graphs (history data is engine state; drawing not yet ported) ------- */

void ChangeCensus()
{ }
void doAllGraphs()
{ }
void graphDoer()
{ }
void initGraphs()
{ }
void InitGraphMax()
{ }
void graph_command_init()
{ }
void drawGraph()
{ }

/* DoShowPicture is provided by kept s_msg.c; DoUpdateHeads and UpdateFunds by
 * kept w_update.c (their UI-notify bodies call Eval / other stubs here). */

/* ---- minimap decode (g_map.c / g_smmaps.c not yet ported) ---------------- */

void setUpMapProcs()
{ }
void drawAll(view)
SimView *view;
{ }

/* ---- sprites (w_sprite.c not yet ported): spawners referenced by
 *      s_disast.c / s_traf.c / s_sim.c, plus MoveObjects from the main loop.
 *      No-op for Phase 0 (no sprites move, no crash-fires start).           */

/* Sprite AI (MoveObjects, Make*, Generate*, GetSprite, StartFire, ...) is now
 * provided by the compiled w_sprite.c.  Only the X-drawing DrawObjects/DrawSprite
 * are dropped there, so DrawObjects keeps its no-op here (nc_render draws sprites). */
void DrawObjects(view)
SimView *view;
{ }

/* Tool logic (DoTool/setWandState/ToolDown/ToolUp/ToolDrag/DoPendTool) is now
 * provided by the compiled w_tool.c. */

/* ---- sound (w_sound.c is kept, but MakeSound may be here for now) --------
 *      MakeSound itself is provided by kept w_sound.c; nothing needed.      */

/* ---- Eval: the engine->UI string bridge (was Tcl_Eval in w_tk.c) ---------
 *
 * Kept engine files emit a handful of fixed "UIxxx" command literals.  For
 * Phase 0 we recognize them and no-op; nc_shim.c will route them to real
 * ncurses handlers later.  Unknown strings are ignored silently.
 */

int Eval(buf)
char *buf;
{
  return 0;
}
