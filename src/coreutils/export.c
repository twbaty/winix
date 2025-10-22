#include <stdio.h>
#include <stdlib.h>

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: export VAR=value\n");return 1;}
    putenv(argv[1]); return 0;
}
