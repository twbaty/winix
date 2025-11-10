#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
  #include <windows.h>

  int main(void) {
      // Windows provides environment variables as a contiguous block
      LPCH env = GetEnvironmentStringsA();  // returns double-NUL-terminated block
      if (!env) return 1;

      // Iterate through the block until the final '\0\0'
      for (LPCH p = env; *p; ) {
          puts(p);           // print "NAME=VALUE"
          while (*p) ++p;    // advance to the end of this string
          ++p;               // advance to the start of the next
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
