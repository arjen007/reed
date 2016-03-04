#include <sys/types.h>

#ifndef PARAMS
#  if defined PROTOTYPES || (defined __STDC__ && __STDC__)
#    define PARAMS(Args) Args
#  else
#    define PARAMS(Args) ()
#  endif
#endif

void mode_string PARAMS ((mode_t mode, char *str));
