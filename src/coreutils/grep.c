#include <stdio.h>
#include <string.h>

int main(int argc,char*argv[]){
    if(argc!=3){fprintf(stderr,"Usage: grep <pattern> <file>\n");return 1;}
    FILE*f=fopen(argv[2],"r");if(!f){perror("grep");return 1;}
    char line[1024];
    while(fgets(line,sizeof(line),f))
        if(strstr(line,argv[1])) fputs(line,stdout);
    fclose(f); return 0;
}
