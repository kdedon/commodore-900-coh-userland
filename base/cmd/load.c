/*
 * Load a driver.
 * #if I8086 will cause the pseudo load of a driver linked
 * with the system image.
 */
#include <stdio.h>
#include <canon.h>
#include <con.h>
#include <l.out.h>
#include <machine.h>

main(argc, argv)
char *argv[];
{
	register int n;
	register FILE *fp;
#if I8086
	register FILE *cfp;	/* CON file pointer */
#endif
	register size_t s;
	register size_t b;
	size_t symb;		/* file offset of the symbol segment */
	register int conflag;
	CON con;
	struct ldsym lds;
	struct ldheader ldh;
	extern int errno;
	extern char *sys_errlist[];

	if (argc < 2)
		panic("Usage: load <driver>");
	if ((fp=fopen(argv[1], "r")) == NULL)
		panic("Cannot open %s", argv[1]);
#if I8086
	if ((cfp=fopen("/dev/kmem", "r")) == NULL)
		panic("Cannot open /dev/kmem");
#endif
	if (fread(&ldh, sizeof(ldh), 1, fp) != 1)
		panic("Not an l.out");
	canint(ldh.l_magic);
	if (ldh.l_magic != L_MAGIC)
		panic("Not an l.out");
	for (n=0; n<NLSEG; n++)
		cansize(ldh.l_ssize[n]);
	b = sizeof(ldh);
	b += ldh.l_ssize[L_SHRI] + ldh.l_ssize[L_PRVI];
	b += ldh.l_ssize[L_SHRD] + ldh.l_ssize[L_PRVD];
	symb = b;
	conflag = 0;
	fseek(fp, (long)b, 0);
	for (s=ldh.l_ssize[L_SYM]; s; s-=sizeof(lds)) {
		if (fread(&lds, sizeof(lds), 1, fp) != 1)
			panic("Bad l.out");
		canvaddr(lds.ls_addr);
		if (strcmp(&lds.ls_id[2], "con_") != 0)
			continue;
		conflag++;
#if I8086
		fseek(cfp, (long)lds.ls_addr, 0);
#else
		b = sizeof(ldh) + lds.ls_addr - (long)dvirt();
#if Z8001
		b = (unsigned)b;
#endif
		fseek(fp, (long)b, 0);	/* b is a size_t: 16 bits here */
#endif
#if I8086
		if (fread(&con, sizeof(con), 1, cfp) != 1)
#else
		if (fread(&con, sizeof(con), 1, fp) != 1)
#endif
			panic("Cannot read configuration");
#if ! I8086
		/* back to the symbol segment, just past the entry just read */
		fseek(fp, (long)symb+ldh.l_ssize[L_SYM]-s+sizeof(lds), 0);
#endif
		if (sload(con.c_mind, argv[1], lds.ls_addr) < 0)
			fprintf( stderr, "load: %.2s: %s\n", lds.ls_id,
				sys_errlist[errno]);
		else
			record(argv[1]);
	}
	if (conflag == 0)
		panic("Configuration table not found");
	exit(0);
}

/*
 * Record which driver was installed, in /etc/console.
 *
 * Nothing else can say.  The console driver is chosen at boot by md.s vidsel,
 * which probes for a video board and patches the path in init's argument vector;
 * by the time a login shell runs, the only trace left is that this program was
 * handed that path.  /etc/profile needs it because the three consoles are not
 * the same terminal: the video ones emulate an H19/Z19 (rec/mm.c), which is a
 * VT52 as far as termcap is concerned, while a serial console is whatever is
 * plugged into it.
 *
 * A failure is ignored on purpose -- a read-only root must still boot, and the
 * profile falls back on its own.
 */
static
record(path)
char *path;
{
	register FILE *fp;
	register char *p, *base;

	for (base = p = path; *p != '\0'; ++p) {
		if (*p == '/')
			base = p + 1;
	}
	if ((fp = fopen("/etc/console", "w")) != NULL) {
		fprintf(fp, "%s\n", base);
		fclose(fp);
	}
}

/*
 * Print out an error message and exit.
 */
panic(a1)
char *a1;
{
	fprintf(stderr, "%r", &a1);
	fprintf(stderr, "\n");
	exit(1);
}
