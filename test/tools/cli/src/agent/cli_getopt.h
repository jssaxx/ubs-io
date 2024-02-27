#ifndef __CLI_GETOPT_H__
#define __CLI_GETOPT_H__

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#include "cli.h"

struct cli_option
{
  const char *name;
  int has_arg;
  int *flag;
  int val;
};

typedef struct tagGETOPT_THREAD_DATA_S
{
    char *_optarg;
    int _optind;
    int ___getopt_initialized;
    char *_nextchar;
    int _optopt;
    int _first_nonopt;
    int _last_nonopt;
    char errmsg[CLI_MAX_ERR_MSG_LEN];
} GETOPT_THREAD_DATA_S;

#define optarg        td->_optarg
#define optind        td->_optind
#define __getopt_initialized  td->___getopt_initialized
#define nextchar      td->_nextchar
#define optopt         td->_optopt
#define first_nonopt  td->_first_nonopt
#define last_nonopt   td->_last_nonopt

int open_getopt(GETOPT_THREAD_DATA_S *td, int argc, char *const *argv, const char *optstring);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __CLI_GETOPT_H__ */

