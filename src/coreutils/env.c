#include <stdio.h>
#include <stdlib.h>

int main(){
    extern char **environ;
    for(char **p=environ;*p;++p) printf("%s\n",*p);
    return 0;
}
