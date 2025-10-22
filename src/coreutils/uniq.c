#include <stdio.h>
#include <string.h>

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: uniq <file>\n");return 1;}
    FILE*f=fopen(argv[1],"r");if(!f){perror("uniq");return 1;}
    char prev[1024]="",line[1024];
    while(fgets(line,sizeof(line),f)){
        if(strcmp(line,prev)!=0){fputs(line,stdout);strcpy(prev,line);}
    }
    fclose(f); return 0;
}
