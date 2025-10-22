#include <windows.h>
#include <stdio.h>

int main(){
    DWORD ms=GetTickCount();
    printf("Uptime: %.2f hours\n",(double)ms/3600000.0);
    return 0;
}
