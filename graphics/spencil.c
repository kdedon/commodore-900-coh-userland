
#define MAXLINES 30
#define STARTX 508
#define STARTY 62
#define ENDX 700
#define ENDY 195
#define MAXX (ENDX-STARTX)
#define MAXY (ENDY-STARTY)
#define MAXCOUNT 2000
short pcount = 0;


struct	LINE  {
	short lx1,ly1,lx2,ly2;
};

struct LINE	penlines[MAXLINES];

pencil()
{
	struct	LINE last,next;
	short	ndx1,ndx2,ndy1,ndy2;
	short	nl;
	/*clearw();*/
	srand(14325);
	for(nl=0;nl<MAXLINES;nl++) 
			    penlines[nl].lx1 = 	
			    penlines[nl].lx2 = 
			    penlines[nl].ly1 = 
			    penlines[nl].ly2 = 0;
	nl = 0;		
	next.lx1 = next.lx2 = next.ly1 = next.ly2 = 0;
	while(pcount++ < MAXCOUNT) {
		if(nl == 0) {
			last = next;
			next.lx1 = irand(MAXX);
			next.lx2 = irand(MAXX);
			next.ly1 = irand(MAXY);
			next.ly2 = irand(MAXY);
			ndx1 = (next.lx1-last.lx1)/MAXLINES;
			ndx2 = (next.lx2-last.lx2)/MAXLINES;
			ndy1 = (next.ly1-last.ly1)/MAXLINES;
			ndy2 = (next.ly2-last.ly2)/MAXLINES;
		}
		fourl(&penlines[nl]);
		penlines[nl].lx1 = last.lx1+nl*ndx1;
		penlines[nl].lx2 = last.lx2+nl*ndx2;
		penlines[nl].ly1 = last.ly1+nl*ndy1;
		penlines[nl].ly2 = last.ly2+nl*ndy2;
		fourl(&penlines[nl]);
		nl = (++nl)%MAXLINES;
	}
}
fourl(l)
struct LINE *l;
{
	line(STARTX+l->lx1,STARTY+l->ly1,STARTX+l->lx2,STARTY+l->ly2);
	line(STARTX+l->lx1,ENDY-(l->ly1), STARTX+l->lx2, ENDY-(l->ly2));
	line(ENDX-(l->lx1), STARTY+l->ly1, ENDX-(l->lx2), STARTY+l->ly2);
	line(ENDX-(l->lx1), ENDY-(l->ly1), ENDX-(l->lx2), ENDY-(l->ly2));
}
