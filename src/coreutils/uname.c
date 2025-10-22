#include <windows.h>
#include <stdio.h>

int main(){
    OSVERSIONINFOEX v={0}; v.dwOSVersionInfoSize=sizeof(v);
    GetVersionEx((OSVERSIONINFO*)&v);
    printf("Windows %lu.%lu build %lu\n",v.dwMajorVersion,v.dwMinorVersion,v.dwBuildNumber);
    return 0;
}
