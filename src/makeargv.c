#include <stdlib.h>
#include <string.h>

int makeargv(const char *s, const char *delimiters, char ***argvp) {
   int i;
   int numtokens;
   const char *snew;
   char *t;

   if ((s == NULL) || (delimiters == NULL) || (argvp == NULL))
      return -1;
   *argvp = NULL;                           
   snew = s + strspn(s, delimiters);  /* snew is real start of string */
   if ((t = malloc(strlen(snew) + 1)) == NULL) 
      return -1; 

                    /* count the number of tokens in s */
   strcpy(t, snew);
   numtokens = 0;
   if (strtok(t, delimiters) != NULL) 
      for (numtokens = 1; strtok(NULL, delimiters) != NULL; numtokens++) ; 

                    /* create argument array for ptrs to the tokens */
   if ((*argvp = malloc((numtokens + 1)*sizeof(char *))) == NULL) {
      free(t);
      return -1; 
   } 
                   /* insert pointers to tokens into the argument array */
   if (numtokens == 0) 
      free(t);
   else {
      strcpy(t, snew);
      **argvp = strtok(t, delimiters);
      for (i = 1; i < numtokens; i++)
          *((*argvp) + i) = strtok(NULL, delimiters);
    } 
                  /* put in the final NULL pointer and return */
    *((*argvp) + numtokens) = NULL;
    return numtokens;
}     
