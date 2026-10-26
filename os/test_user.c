#include <stdio.h>

int main(int argc, char **argv, char **envp)
{
	char str[100];
	if (scanf("%99s", str) == 1)
	{
		printf(str);
		fflush(stdout);
	}

	return 0;
}