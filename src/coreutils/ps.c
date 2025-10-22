#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

int main(){
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32 p; p.dwSize=sizeof(p);
    if(Process32First(snap,&p)){
        do{ printf("%5lu  %s\n",p.th32ProcessID,p.szExeFile);}while(Process32Next(snap,&p));
    }
    CloseHandle(snap); return 0;
}
