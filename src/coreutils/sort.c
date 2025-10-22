#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX 4096
char *lines[MAX];

int cmp(const void*a,const void*b){return strcmp(*(char**)a,*(char**)b);}

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: sort <file>\n");return 1;}
    FILE*f=fopen(argv[1],"r");if(!f){perror("sort");return 1;}
    char buf[1024]; int n=0;
    while(fgets(buf,sizeof(buf),f)&&n<MAX){lines[n]=strdup(buf);n++;}
    fclose(f);
    qsort(lines,n,sizeof(char*),cmp);
    for(int i=0;i<n;i++){fputs(lines[i],stdout);free(lines[i]);}
    return 0;
}
