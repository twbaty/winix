#include <windows.h>
#include <stdio.h>

int main(){
    OSVERSIONINFO v={sizeof(v)};
    GetVersionEx(&v);
    printf("Winix built for Windows %lu.%lu (build %lu)\n",
           v.dwMajorVersion,v.dwMinorVersion,v.dwBuildNumber);
    return 0;
}
