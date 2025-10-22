#include <windows.h>
#include <stdio.h>

int main(int argc,char*argv[]){
    if(argc!=2){fprintf(stderr,"Usage: kill <pid>\n");return 1;}
    DWORD pid=atoi(argv[1]);
    HANDLE h=OpenProcess(PROCESS_TERMINATE,FALSE,pid);
    if(!h){perror("kill");return 1;}
    TerminateProcess(h,0); CloseHandle(h); return 0;
}
