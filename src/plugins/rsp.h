/* 
 * $Id: $
 */

#ifndef _RSP_H_
#define _RSP_H_

#define RSP_VERSION "1.0"

extern PLUGIN_INFO _pi;
#define infn ((PLUGIN_INPUT_FN *)(_pi.fn))

#ifndef TRUE
# define TRUE 1
# define FALSE 0
#endif

#endif /* _RSP_H_ */
