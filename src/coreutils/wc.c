#include <stdio.h>

int main(int argc, char *argv[]) {
    if (argc != 2) { fprintf(stderr,"Usage: wc <file>\n"); return 1; }
    FILE *f=fopen(argv[1],"r"); if(!f){perror("wc");return 1;}
    int c; long lines=0, words=0, bytes=0; int inword=0;
    while((c=fgetc(f))!=EOF){bytes++; if(c=='\n')lines++;
        if((c==' '||c=='\n'||c=='\t')) inword=0;
        else if(!inword){inword=1; words++;}}
    fclose(f);
    printf("%ld %ld %ld %s\n", lines, words, bytes, argv[1]);
    return 0;
}
