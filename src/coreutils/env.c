#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>

  int main(void) {
      LPCH env = GetEnvironmentStringsA();  // returns double-NUL-terminated block
      if (!env) return 1;

      for (LPCH p = env; *p; ) {
          puts(p);
          while (*p) ++p;   // advance to NUL of this string
          ++p;              // move to start of next string (or final NUL)
      }

      FreeEnvironmentStringsA(env);
      return 0;
  }

#else
  extern char **environ;

  int main(void) {
      for (char **p = environ; *p; ++p)
          puts(*p);
      return 0;
  }
#endif
