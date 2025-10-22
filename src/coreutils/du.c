#include <stdio.h>
#include <sys/stat.h>

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: du <file>\n");return 1;}
    struct stat s; if(stat(argv[1],&s)!=0){perror("du");return 1;}
    printf("%lld\t%s\n",(long long)((s.st_size+1023)/1024),argv[1]);
    return 0;
}
