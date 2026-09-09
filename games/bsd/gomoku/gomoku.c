/*
 * GO-MOKU.  COHERENT-adapted single-file version from the MWC BBS collection
 * (built there as `cc gomoku.c -lcurses -lterm'), in preference to the NetBSD
 * game of the same name: this one is already K&R, already targets exactly this
 * 4.x curses, and its evaluator works in small integers, where NetBSD's is
 * built on 32-bit combination bitmasks.
 *
 * Author and terms UNKNOWN.  Neither this file nor the holding it came from
 * carries a copyright notice, an author, or a grant of any kind, and the MWC
 * BBS is where it was found, not who wrote it.  It is not Berkeley's and it is
 * not this project's; see ../COPYING.
 */

/* This program plays a very old Japanese game called GO-MOKU,
   perhaps better known as  5-in-line.   The game is played on
   a board with 19 x 19 squares, and the object of the game is
   to get 5 stones in a row.
*/

#include <curses.h>
#include <ctype.h>
#include "gomoku.h"

/* Size of the board */
#define SIZE 19

/* Importance of attack (1..16) */
#define AttackFactor 4

/* Value of having 0, 1,2,3,4 or 5 pieces in line */
int Weight[7] = {0, 0, 4, 20, 100, 500, 0};

#define Null 0
#define Horiz 1
#define DownLeft 2
#define DownRight 3
#define Vert 4

/* The two players */
#define Empty 0
#define Cross 1
#define Nought 2

char PieceChar[Nought+1] = {' ', 'X', '0'};

short Board[SIZE+1][SIZE+1];	/* The board */
short Player;			/* The player whose move is next */
int TotalLines;			/* The number of Empty lines left */
short GameWon;			/* Set if one of the players has won */

short Line[4][SIZE+1][SIZE+1][Nought+1];

/* Value of each square for each player */
int Value[SIZE+1][SIZE+1][Nought+1];

short X, Y;			/* Move coordinates */
char Command;			/* Command from keyboard */
short AutoPlay = FALSE;		/* The program plays against itself */

/* Get the termcap entry and set terminal to raw mode. */
Initialize()
{
  srand(getpid()+13); /* Initialize the random seed with our pid */
  initscr();
  raw(); noecho();
  clear();
} 

/* Reset terminal and exit from the program. */
Abort(s) char *s;
{
  move(LINES-1, 0);
  refresh();
  endwin();
  exit(0);
}

/* set up the screen ----------------------------------------------- */

/* Write the letters */
WriteLetters()
{
  int i;

  addch(' '); addch(' ');
  for (i=1; i<=SIZE; i++) printw(" %c",'A' + i - 1);
  addch('\n');
}

/* Write one line of the board */
WriteLine(j,s) int j; int *s;
{
  int i;

  printw("%2d ",j); addch(s[0]);
  for (i=2; i<=SIZE-1; i++) { addch(s[1]); addch(s[2]); }
  addch(s[1]); addch(s[3]); printw(" %-2d\n", j);
}

/* Print the Empty board and the border */
WriteBoard(N, Top, Middle,Bottom) int N; int *Top, *Middle, *Bottom;
{
  int j;

  move(1, 0);
  WriteLetters();
  WriteLine(N, Top);
  for (j=N-1; j>=2; j--) WriteLine(j, Middle);
  WriteLine(1, Bottom);
  WriteLetters();
}

/* Sets up the screen with an Empty board */
SetUpScreen()
{
  int top[4], middle[4], bottom[4];

  top[0]=ACS_ULCORNER; top[1]=ACS_HLINE;
  top[2]=ACS_TTEE; top[3]=ACS_URCORNER;
  
  middle[0]=ACS_LTEE; middle[1]=ACS_HLINE;
  middle[2]=ACS_PLUS; middle[3]=ACS_RTEE;
  
  bottom[0]=ACS_LLCORNER; bottom[1]=ACS_HLINE;
  bottom[2]=ACS_BTEE; bottom[3]=ACS_LRCORNER;
  
  WriteBoard(SIZE, top, middle, bottom);
}

/* show moves ----------------------------------------------- */

GotoSquare(x, y) int x,y;
{
  move(SIZE + 2 - y, 1 + x * 2);
}

/* Prints a move */
PrintMove(Piece,X,Y) int Piece; short X, Y;
{
  move(22, 49);
  printw("%c %c %d",PieceChar[Piece], 'A' + X - 1, Y);
  clrtoeol();
  GotoSquare(X, Y);
  addch(PieceChar[Piece]);
  GotoSquare(X, Y);
  refresh();
}

/* Clears the line where a move is displayed */
ClearMove()
{
  move(22, 49);
  clrtoeol();
}

/* message handling ---------------------------------------------- */

/* Prints a message */
PrintMsg(Str) char *Str;
{
  mvprintw(23,1,"%s",Str);
}

/* Clears the message about the winner */
ClearMsg()
{
  move(23,1);
  clrtoeol();
}

/* Highlights the first letter of S */
WriteCommand(S) char *S;
{
  standout();
  addch(*S);
  standend();
  printw("%s",S+1);
}

/* display the board ----------------------------------------------- */

/* Resets global variables to start a new game */
ResetGame(FirstGame) short FirstGame;
{
  short I, J;
  short C,D;

  SetUpScreen();
  if (FirstGame)
  {
    move(1, 49);
    addstr("G O M O K U");
    move(3, 49); WriteCommand("Newgame    ");
    WriteCommand("Quit ");
    move(5, 49); WriteCommand("Auto");
    move(7, 49); WriteCommand("Play");
    move(9, 49); WriteCommand("Hint");
    move(14, 60); WriteCommand("Left, "); WriteCommand("Right, ");
    move(16, 60); WriteCommand("Up, "); WriteCommand("Down");
    move(18, 60); standout(); addstr("SPACE"); standend();
    mvaddstr(14, 49, "7  8  9");
    mvaddch(15, 52, ACS_UARROW);
    mvaddch(16, 49, '4');
    addch(ACS_LARROW); mvaddch(16, 54, ACS_RARROW);
    addch('6');
    mvaddch(17, 52, ACS_DARROW);
    mvaddstr(18, 49, "1  2  3");
    FirstGame = FALSE;
  }
  else
  {
    ClearMsg();
    ClearMove();
  }
  /* Clear tables */
  for (I=1; I<=SIZE; I++) for (J=1; J<=SIZE; J++)
  {
    Board[I][J] = Empty;
    for (C=Cross; C<=Nought; C++)
    {
      Value[I][J][C] = 0;
      for (D=0; D<=3; D++) Line[D][I][J][C] = 0;
    }
  }
  /* Cross starts */
  Player = Cross;
  /* Total number of lines */
  TotalLines = 2*2*(SIZE*(SIZE-4)+(SIZE-4)*(SIZE-4));
  GameWon = FALSE;
}

short OpponentColor(Player) short Player;
{
  if (Player == Cross) return Nought;
  else return Cross;
}

/* Blink the row of 5 stones */
BlinkRow(X,Y,Dx,Dy,Piece) int X, Y, Dx, Dy, Piece;
{
  int I;
  
  /*attron(A_BLINK);*/
  for (I=1; I<=5; I++)
  {
    GotoSquare(X, Y);
    addch(PieceChar[Piece]);
    X = X - Dx;
    Y = Y - Dy;
  }
  /*attroff(A_BLINK);*/
}

/* Prints the 5 winning stones in blinking color */
BlinkWinner(Piece,X,Y,WinningLine) short Piece,X,Y,WinningLine;
{
  /* Used to store the position of the winning move */
  int XHold, YHold;
  /* Change in X and Y */
  int Dx, Dy;

  /* display winning move */
  PrintMove(Piece, X, Y);
  /* preserve winning position */
  XHold = X;
  YHold = Y;
  switch (WinningLine)
  {
    case Horiz :
    {
      Dx = 1;
      Dy = 0;
      break;
    }

    case DownLeft :
    {
      Dx = 1;
      Dy = 1;
      break;
    }

    case Vert :
    {
      Dx = 0;
      Dy = 1;
      break;
    }

    case DownRight :
    {
      Dx = -1;
      Dy = 1;
      break;
    }
  }
  /* go to topmost, leftmost */
  while (Board[X+Dx][Y+Dy]!=Empty && Board[X+Dx][Y+Dy]==Piece)
  {
    X = X + Dx;
    Y = Y + Dy;
  }
  BlinkRow(X, Y, Dx, Dy, Piece);
  /* restore winning position */
  X = XHold;
  Y = YHold;
  /* go back to winning square */
  GotoSquare(X, Y);
}

/* functions for playing a game -------------------------------- */

int Random(x) int x;
{
  return ((rand()/19) % x);
}

/* Adds one to the number of pieces in a line */
Add(Num) short *Num;
{
  /* Adds one to the number.     */
  *Num = *Num + 1;
  /* If it is the first piece in the line, then the opponent cannot use it
  any more.  */
  if (*Num == 1) TotalLines = TotalLines - 1;
  /* The game is won if there are 5 in line. */
  if (*Num == 5) GameWon = TRUE;
}

/* Updates the value of a square for each player, taking into
   account that player has placed an extra piece in the square.
   The value of a square in a usable line is Weight[Lin[Player]+1]
   where Lin[Player] is the number of pieces already placed
in the line */
Update(Lin,Valu,Opponent) short Lin[]; int Valu[]; short Opponent;
{
  /* If the opponent has no pieces in the line, then simply update the 
  value for player */
  if (Lin[Opponent] == 0) 
  Valu[Player]+= Weight[Lin[Player]+1]-Weight[Lin[Player]];
  else
  /* If it is the first piece in the line, then the line is
  spoiled for the opponent */
  if (Lin[Player] == 1) Valu[Opponent]-= Weight[Lin[Opponent] + 1];
}

/* Performs the move X,Y for player, and updates the global variables
(Board, Line, Value, Player, GameWon, TotalLines and the screen) */
MakeMove(X, Y) short X,Y;
{
  short Opponent;
  int X1 ,Y1;
  short K, L, WinningLine;

  WinningLine = Null;
  Opponent = OpponentColor(Player);
  GameWon = FALSE;

  /* Each square of the board is part of 20 different lines.
     The adds one to the number of pieces in each
     of these lines. Then it updates the value for each of the 5
     squares in each of the 20 lines. Finally Board is updated, and
  the move is printed on the screen. */

  /* Horizontal lines, from left to right */
  for (K=0; K<=4; K++)
  {
    X1 = X - K;                           /* Calculate starting point */
    Y1 = Y;
    if ((1 <= X1) && (X1 <= SIZE - 4))        /* Check starting point */
    {
      Add(&Line[0][X1][Y1][Player]);                 /* Add one to line */
      if (GameWon && (WinningLine == Null))    /* Save winning line */
      WinningLine = Horiz;
      for (L=0; L<=4; L++) /* Update value for the 5 squares in the line */
      Update(Line[0][X1][Y1], Value[X1 + L][Y1], Opponent);
    }
  }

  for (K=0; K<=4; K++) /* Diagonal lines, from lower left to upper right */
  {
    X1 = X - K;
    Y1 = Y - K;
    if ((1 <= X1) && (X1 <= SIZE - 4) &&
    (1 <= Y1) && (Y1 <= SIZE - 4))
    {
      Add(&Line[1][X1][Y1][Player]);
      if (GameWon && (WinningLine == Null))    /* Save winning line */
      WinningLine = DownLeft;
      for (L=0; L<=4; L++)
      Update(Line[1][X1][Y1], Value[X1 + L][Y1 + L], Opponent);
    }
  } /* for */

  for (K=0; K<=4; K++)       /* Diagonal lines, down right to upper left */
  {
    X1 = X + K;
    Y1 = Y - K;
    if ((5 <= X1) && (X1 <= SIZE) &&
    (1 <= Y1) && (Y1 <= SIZE - 4))
    {
      Add(&Line[3][X1][Y1][Player]);
      if (GameWon && (WinningLine == Null))    /* Save winning line */
      WinningLine = DownRight;
      for (L=0; L<=4; L++)
      Update(Line[3][X1][Y1], Value[X1 - L][Y1 + L], Opponent);
    }
  } /* for */

  for (K=0; K<=4; K++)                /* Vertical lines, from down to up */
  {
    X1 = X;
    Y1 = Y - K;
    if ((1 <= Y1) && (Y1 <= SIZE - 4))
    {
      Add(&Line[2][X1][Y1][Player]);
      if (GameWon && (WinningLine == Null))    /* Save winning line */
      WinningLine = Vert;
      for (L=0; L<=4; L++)
      Update(Line[2][X1][Y1], Value[X1][Y1 + L], Opponent);
    }
  }

  Board[X][Y] = Player;             /* Place piece in board */
  if (GameWon) BlinkWinner(Player, X, Y, WinningLine);
  else PrintMove(Player, X, Y);         /* Print move on screen */
  Player = Opponent;        /* The opponent is next to move */
}

short GameOver()
/* A game is over if one of the players have
won, or if there are no more Empty lines */
{
  return (GameWon || (TotalLines <= 0));
}

/* Finds a move X,Y for player, simply by picking the one with the
highest value */
FindMove(X,Y) short *X, *Y;
{
  short Opponent;
  int I, J;
  /* Value[][][] reaches four directions x five line positions x Weight[5],
   * i.e. 10000, and the attack scaling multiplies that by 20 before dividing:
   * 200000 does not fit in a 16-bit int, and a wrapped score picks a random
   * square.  The comparison is the only use, so a long costs nothing. */
  long Max, Valu;

  Opponent = OpponentColor(Player);
  Max = -10000L;
  /* If no square has a high value then pick the one in the middle */
  *X = (SIZE + 1) / 2;
  *Y = (SIZE + 1) / 2;
  if (Board[*X][*Y]==Empty) Max=4L;
  /* The evaluation for a square is simply the value of the square
    for the player (attack points) plus the value for the opponent
    (defense points). Attack is more important than defense, since
    it is better to get 5 in line yourself than to prevent the op-
  ponent from getting it. */

  /* For all Empty squares */
  for (I=1; I<=SIZE; I++) for (J=1; J<=SIZE; J++)
  if (Board[I][J] == Empty)
  {
    /* Calculate evaluation */
    Valu = (long)Value[I][J][Player]*(16+AttackFactor)/16
	 + Value[I][J][Opponent] + Random(4);
    /* Pick move with highest value */
    if (Valu>Max)
    {
      *X = I;
      *Y = J;
      Max = Valu;
    }
  }
}

char GetChar()
/* Get a character from the keyboard */
{
  int c;

  c=getch();
  if (c<0) abort();
  if (islower(c)) return toupper(c); else return c;
}

/* Reads in a valid command character */
ReadCommand(X, Y, Command) short X,Y; char *Command;
{
  short ValidCommand;

  do
  {
    ValidCommand = TRUE;
    GotoSquare(X, Y);                                    /* Goto square */
    refresh();
    *Command=GetChar();                             /* Read from keyboard */
    switch(*Command)
    {
      case '\n':                           /* '\n' or space means place a */
      case ' ' : *Command = 'E'; break;       /* stone at the cursor position  */
      case 'L':
      case 'R':
      case 'D':
      case 'U':
      case '7':
      case '9':
      case '1':
      case '3':
      case 'N':
      case 'Q':
      case 'A':
      case 'P':
      case 'H' : break;
      case '8' : *Command = 'U'; break;
      case '2' : *Command = 'D'; break;
      case '4' : *Command = 'L'; break;
      case '6' : *Command = 'R'; break;
      default:
      {
        if (GameOver()) *Command='P';
        else ValidCommand = FALSE;
        break;
      }
    }
  } while (!ValidCommand);
}

InterpretCommand(Command) char Command;
{
  int Temp;

  switch(Command)
  {
    case 'N': {                                        /* Start new game */
      ResetGame(FALSE);     /* ResetGame but only redraw the board */
      X = (SIZE + 1) / 2;
      Y = X;
      break;
    }
    case 'H': FindMove(&X, &Y); break;               /* Give the user a hint */
    case 'L': X = (X + SIZE - 2) % SIZE + 1; break;             /* Left  */
    case 'R': X = X % SIZE + 1; break;                           /* Right */
    case 'D': Y = (Y + SIZE - 2) % SIZE + 1; break;                 /* Down  */
    case 'U': Y = Y % SIZE + 1; break;                           /* Up    */
    case '7': {
      if ((X == 1) || (Y == SIZE))    /* Move diagonally    */
      {                         /* towards upper left */
        Temp = X;
        X = Y;
        Y = Temp;
      }
      else
      {
        X = X - 1;
        Y = Y + 1;
      }
      break;
    }
    case '9': {                           /* Move diagonally    */
      if (X == SIZE)                 /* toward upper right */
      {
        X = (SIZE - Y) + 1;
        Y = 1;
      }
      else if (Y == SIZE)
      {
        Y = (SIZE - X) + 1;
        X = 1;
      }
      else
      {
        X = X + 1;
        Y = Y + 1;
      }
      break;
    }
    case'1': {                            /* Move diagonally   */
      if (Y == 1)                  /* toward lower left */
      {
        Y = (SIZE - X) + 1;
        X = SIZE;
      }
      else if (X == 1)
      {
        X = (SIZE - Y) + 1;
        Y = SIZE;
      }
      else
      {
        X = X - 1;
        Y = Y - 1;
      }
      break;
    }
    case '3': {                           /* Move diagonally    */
      if ((X == SIZE) || (Y == 1))    /* toward lower right */
      {
        Temp = X;
        X = Y;
        Y = Temp;
      }
      else
      {
        X = X + 1;
        Y = Y - 1;
      }
      break;
    }
    case 'A': AutoPlay = TRUE; break;                  /* Auto play mode */
  } /* case */
} /* InterpretCommand */

PlayerMove()
/* Enter and make a move */
{
  if (Board[X][Y] == Empty)
  {
    MakeMove(X, Y);
    if (GameWon) PrintMsg("Congratulations, You won!");
    Command = 'P';
  }
  refresh();
} /* PlayerMove */

ProgramMove()
/* Find and perform programs move */
{
  do
  {
    if (GameOver()) 
    {
      AutoPlay = FALSE;
      if ((Command != 'Q') && (!GameWon)) PrintMsg("Tie game!");
    }
    else
    {
      FindMove(&X, &Y);
      MakeMove(X, Y);
      if (GameWon) PrintMsg("I won!");
    }
    refresh();
  } while (AutoPlay);
}

main()
{
  Initialize();
  ResetGame(TRUE);     /* ResetGame and draw the entire screen */
  refresh();
  X = (SIZE + 1) / 2;              /* Set starting position to */
  Y = X;                          /* the middle of the board  */
  do
  {
    ReadCommand(X, Y, &Command);
    if (GameOver()) if (Command != 'Q') Command = 'N';
    InterpretCommand(Command);
    if (Command == 'E') PlayerMove();
    if (Command == 'P' || Command == 'A') ProgramMove();
  } while (Command!='Q');
  Abort("Good bye!");
}
