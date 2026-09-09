#define MAX 800



main()
{
	int	i;

	printf("Draw some lines");


	for (i = 0; i < MAX; i += 5) 
		line(MAX - i, i, 0, 0);


}
