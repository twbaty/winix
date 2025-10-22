#include <stdio.h>
#include <windows.h>

int main(){
    ULARGE_INTEGER freeBytes, totalBytes, totalFree;
    if(GetDiskFreeSpaceEx(NULL,&freeBytes,&totalBytes,&totalFree)){
        printf("Total: %.2f GB\nFree : %.2f GB\n",
            (double)totalBytes.QuadPart/1e9,
            (double)freeBytes.QuadPart/1e9);
        return 0;
    }
    perror("df"); return 1;
}
