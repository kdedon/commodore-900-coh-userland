#include "sim.h"


/* City Evaluation */


short EvalValid;
short CityYes, CityNo;
short ProblemTable[PROBNUM];
short ProblemTaken[PROBNUM];
short ProblemVotes[PROBNUM];		/* these are the votes for each  */
short ProblemOrder[4];			/* sorted index to above  */
QUAD CityPop, deltaCityPop;
QUAD CityAssValue;
short CityClass;			/*  0..5  */
short CityScore, deltaCityScore, AverageCityScore;
short TrafficAverage;


/* comefrom: SpecialInit Simulate */
int CityEvaluation()
{
  EvalValid = 0;
  if (TotalPop) {
    GetAssValue();
    DoPopNum();
    DoProblems();
    GetScore();
    DoVotes();
    ChangeEval();
  } else {
    EvalInit();
    ChangeEval();
  }
  EvalValid = 1;
}


/* comefrom: CityEvaluation SetCommonInits */
int EvalInit()
{
  register short x, z;

  z = 0;
  CityYes = z;
  CityNo = z;
  CityPop = z;
  deltaCityPop = z;
  CityAssValue = z;
  CityClass = z;
  CityScore = 500;
  deltaCityScore = z;
  EvalValid = 1;
  for (x = 0; x < PROBNUM; x++)
    ProblemVotes[x] = z;
  for (x = 0; x < 4; x++)
    ProblemOrder[x] = z;
}


/* comefrom: CityEvaluation */
int GetAssValue()
{
  QUAD z;

  /* Every count here is a short and every weight is large enough to leave a
   * 16-bit product: one airport at 10000 needs four to overflow, one police
   * station at 1000 needs thirty-three.  Each multiply is done in QUAD. */
  z = RoadTotal * 5L;
  z += RailTotal * 10L;
  z += PolicePop * 1000L;
  z += FireStPop * 1000L;
  z += HospPop * 400L;
  z += StadiumPop * 3000L;
  z += PortPop * 5000L;
  z += APortPop * 10000L;
  z += CoalPop * 3000L;
  z += NuclearPop * 6000L;
  CityAssValue = z * 1000L;
}


/* comefrom: CityEvaluation */
int DoPopNum()
{
  QUAD OldCityPop;

  OldCityPop = CityPop;
  CityPop = ((ResPop) + (ComPop * 8L) + (IndPop *8L)) * 20L;
  if (OldCityPop == -1) {
    OldCityPop = CityPop;
  }
  deltaCityPop = CityPop - OldCityPop;

  CityClass = 0;			/* village */
  if (CityPop > 2000)	CityClass++;	/* town */
  if (CityPop > 10000)	CityClass++;	/* city */
  if (CityPop > 50000)	CityClass++;	/* capital */
  if (CityPop > 100000)	CityClass++;	/* metropolis */
  if (CityPop > 500000)	CityClass++;	/* megalopolis */
}


/* comefrom: CityEvaluation */
int DoProblems()
{
  register short x, z;
  short ThisProb, Max;

  for (z = 0; z < PROBNUM; z++)
    ProblemTable[z] = 0;
  ProblemTable[0] = CrimeAverage;		/* Crime */
  ProblemTable[1] = PolluteAverage;		/* Pollution */
  ProblemTable[2] = LVAverage * .7;		/* Housing */
  ProblemTable[3] = CityTax * 10;		/* Taxes */
  ProblemTable[4] = AverageTrf();		/* Traffic */
  ProblemTable[5] = GetUnemployment();		/* Unemployment */
  ProblemTable[6] = GetFire();			/* Fire */
  VoteProblems();
  for (z = 0; z < PROBNUM; z++)
    ProblemTaken[z] = 0;
  for (z = 0; z < 4; z++) {
    Max = 0;
    for (x = 0; x < 7; x++) {
      if ((ProblemVotes[x] > Max) && (!ProblemTaken[x])) {
	ThisProb = x;
	Max = ProblemVotes[x];
      }
    }
    if (Max) {
      ProblemTaken[ThisProb] = 1;
      ProblemOrder[z] = ThisProb;
    }
    else {
      ProblemOrder[z] = 7;
      ProblemTable[7] = 0;
    }
  }
}


/* comefrom: DoProblems */
int VoteProblems()
{
  register x, z, count;

  for (z = 0; z < PROBNUM; z++)
    ProblemVotes[z] = 0;

  x = 0;
  z = 0;
  count = 0;
  while ((z < 100) && (count < 600)) {
    if (Rand(300) < ProblemTable[x]) {
      ProblemVotes[x]++;
      z++;
    }
    x++;
    if (x > PROBNUM) x = 0;
    count++;
  }
}


/* comefrom: DoProblems */
int AverageTrf()
{
  QUAD TrfTotal;
  register short x, y, count;

  TrfTotal = 0;
  count = 1;
  for (x=0; x < HWLDX; x++)
    for (y=0; y < HWLDY; y++)
      if (LandValueMem[x][y]) {
	TrfTotal += TrfDensity[x][y];
	count++;
      }

  TrafficAverage = (TrfTotal / count) * 2.4;
  return(TrafficAverage);
}


/* comefrom: DoProblems */
int GetUnemployment()
{
  double r;
  short b;

  b = (ComPop + IndPop) << 3;
  if (b)
    r = ((double)ResPop) / b;
  else
    return(0);

  b = (r - 1) * 255;
  if (b > 255)
    b = 255;
  return (b);
}


/* comefrom: DoProblems GetScore */
int GetFire()
{
  short z;

  z = FirePop * 5;
  if (z > 255)
    return(255);
  else
    return(z);
}


/* comefrom: CityEvaluation */
int GetScore()
{
  register x, z;
  short OldCityScore;
  double SM, TM;

  OldCityScore = CityScore;
  x = 0;
  for (z = 0; z < 7; z++)
    x += ProblemTable[z];	/* add 7 probs */

  x = x / 3;			/* 7 + 2 average */
  if (x > 256) x = 256;

  z = (256 - x) * 4;
  if (z > 1000) z = 1000;
  if (z < 0 ) z = 0;

  if (ResCap) z = z * .85;
  if (ComCap) z = z * .85;
  if (IndCap) z = z * .85;
  if (RoadEffect < 32)  z = z - (32 - RoadEffect);
  if (PoliceEffect < 1000) z = z * (.9 + (PoliceEffect / 10000.1));
  if (FireEffect < 1000) z = z * (.9 + (FireEffect / 10000.1));
  if (RValve < -1000) z = z * .85;
  if (CValve < -1000) z = z * .85;
  if (IValve < -1000) z = z * .85;

  SM = 1.0;
  if ((CityPop == 0) || (deltaCityPop == 0))
    SM = 1.0;
  else if (deltaCityPop == CityPop)
    SM = 1.0;
  else if (deltaCityPop > 0)
    SM = ((double)deltaCityPop/CityPop) + 1.0;
  else if (deltaCityPop < 0)
    SM = .95 + ((double) deltaCityPop/(CityPop - deltaCityPop));
  z = z * SM;
  z = z - GetFire();		/* dec score for fires */
  z = z - (CityTax);

  TM = unPwrdZCnt + PwrdZCnt;	/* dec score for unpowered zones */
  if (TM) SM = PwrdZCnt / TM;
  else SM = 1.0;
  z = z * SM;

  if (z > 1000) z = 1000;
  if (z < 0 ) z = 0;

  CityScore = (CityScore + z) / 2;

  deltaCityScore = CityScore - OldCityScore;
}


/* comefrom: CityEvaluation */
int DoVotes()
{
  register z;

  CityYes = 0;
  CityNo = 0;
  for (z = 0; z < 100; z++) {
    if (Rand(1000) < CityScore)
      CityYes++;
    else
      CityNo++;
  }
}
