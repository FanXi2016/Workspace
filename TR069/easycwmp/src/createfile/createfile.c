#include <stdio.h>      // fseek(); fwrite();
#include <string.h>     // memset();
#include <unistd.h>     // truncate();

int main (int argc, char **argv)
{
	if (argc < 3)
	{
		printf("invalid parameter");
		return 1;
	}
	int ret = -1; 
	char *filename = argv[1];
	int filelen = atoi(argv[2]);
	int nlen = filelen;
	FILE *fp = NULL;
	fp = fopen (filename, "w");
	char buf[1024];
	memset(buf, '1', sizeof(buf));

	int ncount = 0;
	while ( ncount < filelen && nlen > 1024)
	{
		ret = fwrite (buf, 1024, 1, fp);
		nlen-=1024;
		ncount+=1024;
	}

	for (;ncount < filelen; ncount++)
	{
		ret = fwrite ("1", 1, 1, fp);
	}

	fclose (fp);

	return 0;
}
