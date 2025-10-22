#include <stdio.h>
#include <string.h>

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: tee <file>\n");return 1;}
    FILE*out=fopen(argv[1],"w");if(!out){perror("tee");return 1;}
    char buf[1024];
    while(fgets(buf,sizeof(buf),stdin)){fputs(buf,stdout);fputs(buf,out);}
    fclose(out); return 0;
}
