#include "sim.h"


double roadPercent = 0.0;
double policePercent = 0.0;
double firePercent = 0.0;
QUAD roadValue;
QUAD policeValue;
QUAD fireValue;
QUAD roadMaxValue;
QUAD policeMaxValue;
QUAD fireMaxValue;
int MustDrawCurrPercents = 0;
int MustDrawBudgetWindow = 0;
int SetBudget();


void InitFundingLevel()
{
  firePercent = 1.0;		/* 1.0 */
  fireValue = 0;
  policePercent = 1.0;		/* 1.0 */
  policeValue = 0;
  roadPercent = 1.0;		/* 1.0 */
  roadValue = 0;
  drawBudgetWindow();
  drawCurrPercents();
}


int DoBudget()
{
  DoBudgetNow(0);
}


int DoBudgetFromMenu()
{
  DoBudgetNow(1);
}


int DoBudgetNow(fromMenu)
int fromMenu;
{
  QUAD yumDuckets;
  QUAD total;
  QUAD moreDough;
  QUAD fireInt, policeInt, roadInt;

  fireInt = (int)(((double)FireFund) * firePercent);
  policeInt = (int)(((double)PoliceFund) * policePercent);
  roadInt = (int)(((double)RoadFund) * roadPercent);

  total = fireInt + policeInt + roadInt;

  yumDuckets = TaxFund + TotalFunds;

  if (yumDuckets > total) {
    fireValue = fireInt;
    policeValue = policeInt;
    roadValue = roadInt;
  } else if (total > 0) {
    if (yumDuckets > roadInt) {
      roadValue = roadInt;
      yumDuckets -= roadInt;

      if (yumDuckets > fireInt) {
	fireValue = fireInt;
	yumDuckets -= fireInt;

	if (yumDuckets > policeInt) {
	  policeValue = policeInt;
	  yumDuckets -= policeInt;
	} else {
	  policeValue = yumDuckets;
	  if (yumDuckets > 0)
	    policePercent = ((double)yumDuckets) / ((double)PoliceFund);
	  else
	    policePercent = 0.0;
	}
      } else {
	fireValue = yumDuckets;
	policeValue = 0;
	policePercent = 0.0;
	if (yumDuckets > 0)
	  firePercent = ((double)yumDuckets) / ((double)FireFund);
	else
	  firePercent = 0.0;
      }
    } else {
      roadValue = yumDuckets;
      if (yumDuckets > 0)
	roadPercent = ((double)yumDuckets) / ((double)RoadFund);
      else
	roadPercent = 0.0;

      fireValue = 0;
      policeValue = 0;
      firePercent = 0.0;
      policePercent = 0.0;
    }
  } else {
    fireValue = 0;
    policeValue = 0;
    roadValue = 0;
    firePercent = 1.0;
    policePercent = 1.0;
    roadPercent = 1.0;
  }

  fireMaxValue = FireFund;
  policeMaxValue = PoliceFund;
  roadMaxValue = RoadFund;

  drawCurrPercents();

 noMoney:
  if ((!autoBudget) || fromMenu) {
    if (!autoBudget) {
      /* TODO: append the the current year to the budget string */
    }

    ShowBudgetWindowAndStartWaiting();

    if (!fromMenu) {
      FireSpend = fireValue;
      PoliceSpend = policeValue;
      RoadSpend = roadValue;

      total = FireSpend + PoliceSpend + RoadSpend;
      moreDough = (QUAD)(TaxFund - total);
      Spend(-moreDough);
    }
    drawBudgetWindow();
    drawCurrPercents();
    DoUpdateHeads();

  } else { /* autoBudget & !fromMenu */
    if ((yumDuckets) > total) {
      moreDough = (QUAD)(TaxFund - total);
      Spend(-moreDough);
      FireSpend = FireFund;
      PoliceSpend = PoliceFund;
      RoadSpend = RoadFund;
      drawBudgetWindow();
      drawCurrPercents();
      DoUpdateHeads();
    } else {
      autoBudget = 0; /* XXX: force autobudget */
      MustUpdateOptions = 1;
      ClearMes();
      SendMes(29);
      goto noMoney;
    }
  }
}


int drawBudgetWindow()
{
  MustDrawBudgetWindow = 1;
}


int ReallyDrawBudgetWindow()
{
  QUAD cashFlow, cashFlow2;	/* a budget exceeds a 16-bit int */
  char numStr[256], dollarStr[256], collectedStr[256],
       flowStr[256], previousStr[256], currentStr[256];

  cashFlow = TaxFund - fireValue - policeValue - roadValue;

  cashFlow2 = cashFlow;
  if (cashFlow < 0)   {
    cashFlow = -cashFlow;
    sprintf(numStr, "%ld", (long)cashFlow);
    makeDollarDecimalStr(numStr, dollarStr);
    sprintf(flowStr, "-%s", dollarStr);
  } else {
    sprintf(numStr, "%ld", (long)cashFlow);
    makeDollarDecimalStr(numStr, dollarStr);
    sprintf(flowStr, "+%s", dollarStr);
  }

  sprintf(numStr, "%ld", (long)TotalFunds);
  makeDollarDecimalStr(numStr, previousStr);

  sprintf(numStr, "%ld", (long)(cashFlow2 + TotalFunds));
  makeDollarDecimalStr(numStr, currentStr);

  sprintf(numStr, "%ld", (long)TaxFund);
  makeDollarDecimalStr(numStr, collectedStr);

  SetBudget(flowStr, previousStr, currentStr, collectedStr, CityTax);
}


int drawCurrPercents()
{
  MustDrawCurrPercents = 1;
}


int ReallyDrawCurrPercents()
{
  char num[256];
  char fireWant[256], policeWant[256], roadWant[256];
  char fireGot[256], policeGot[256], roadGot[256];

  sprintf(num, "%ld", (long)fireMaxValue);
  makeDollarDecimalStr(num, fireWant);

  sprintf(num, "%ld", (long)policeMaxValue);
  makeDollarDecimalStr(num, policeWant);

  sprintf(num, "%ld", (long)roadMaxValue);
  makeDollarDecimalStr(num, roadWant);

  sprintf(num, "%ld", (long)(fireMaxValue * firePercent));
  makeDollarDecimalStr(num, fireGot);

  sprintf(num, "%ld", (long)(policeMaxValue * policePercent));
  makeDollarDecimalStr(num, policeGot);

  sprintf(num, "%ld", (long)(roadMaxValue * roadPercent));
  makeDollarDecimalStr(num, roadGot);

  SetBudgetValues(roadGot, roadWant,
		  policeGot, policeWant,
		  fireGot, fireWant);
}


int UpdateBudgetWindow()
{
  if (MustDrawCurrPercents) {
    ReallyDrawCurrPercents();
    MustDrawCurrPercents = 0;
  }
  if (MustDrawBudgetWindow) {
    ReallyDrawBudgetWindow();
    MustDrawBudgetWindow = 0;
  }
}


int UpdateBudget()
{
  drawCurrPercents();
  drawBudgetWindow();
  Eval("UIUpdateBudget");
}


int ShowBudgetWindowAndStartWaiting()
{
  /* ncurses port: run the modal budget dialog inline.  Its nested input loop
   * freezes the simulation while open (no SimFrame runs), so no Pause() needed. */
  nc_budget_modal();
}


int SetBudget(flowStr, previousStr, currentStr, collectedStr, tax)
char *flowStr;
char *previousStr;
char *currentStr;
char *collectedStr;
short tax;
{
  char buf[256];

  sprintf(buf, "UISetBudget {%s} {%s} {%s} {%s} {%d}",
	  flowStr, previousStr, currentStr, collectedStr, tax);
  Eval(buf);
}


int SetBudgetValues(roadGot, roadWant, policeGot, policeWant, fireGot, fireWant)
char *roadGot;
char *roadWant;
char *policeGot;
char *policeWant;
char *fireGot;
char *fireWant;
{
  char buf[256];

  sprintf(buf, "UISetBudgetValues {%s} {%s} %d {%s} {%s} %d {%s} {%s} %d",
	  roadGot, roadWant, (int)(roadPercent * 100),
	  policeGot, policeWant, (int)(policePercent * 100),
	  fireGot, fireWant, (int)(firePercent * 100));
  Eval(buf);
}


