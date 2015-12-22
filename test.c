#include <stdio.h>
int main() {
	const char *keywords[] = {"if", "then", "else"};
	int all = (int)sizeof(keywords), each = (int)sizeof(char *), length = all / each;
	printf("All %d, each %d, length %d\n", all, each, length);
	return 0;
}
