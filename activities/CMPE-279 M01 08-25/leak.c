#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char * mystrcpy(char *src)
{
  return strdup(src);
}

int main()
{
  char *cp = mystrcpy("this is a long string");
  printf("%s\n", cp);
}
