// #include "dpax_lvos.h"
#include "cli_def.h"
#include "cli_getopt.h"

static enum
{
    REQUIRE_ORDER, PERMUTE, RETURN_IN_ORDER
} ordering;

/* Value of POSIXLY_CORRECT environment variable.  */
static char *posixly_correct = NULL;

static char * my_index (const char *str, int chr)
{
    while (*str)
    {
        if (*str == chr)
        {
            return (char *) str;
        }

        str++;
    }

    return 0;
}

#define SWAP_FLAGS(ch1, ch2)

static void exchange(GETOPT_THREAD_DATA_S *td, char **argv)
{
    int bottom = first_nonopt;
    int middle = last_nonopt;
    int top = optind;
    char *tem;

    while (top > middle && middle > bottom)
    {
        if (top - middle > middle - bottom)
        {
            /* Bottom segment is the short one.  */
            int len = middle - bottom;
            register int i;

            /* Swap it with the top part of the top segment.  */
            for (i = 0; i < len; i++)
            {
                int tmpLen = top - (middle - bottom);
                tem = argv[bottom + (long)i];
                argv[bottom + (long)i] = argv[tmpLen + (long)i];
                argv[tmpLen + (long)i] = tem;
                SWAP_FLAGS (bottom + i, tmpLen + i);
            }

            /* Exclude the moved bottom segment from further swapping.  */
            top -= len;
        }
        else
        {
            /* Top segment is the short one.  */
            int len = top - middle;
            register int i;

            /* Swap it with the bottom part of the bottom segment.  */
            for (i = 0; i < len; i++)
            {
                tem = argv[bottom + (long)i];
                argv[bottom + (long)i] = argv[middle + (long)i];
                argv[middle + (long)i] = tem;
                SWAP_FLAGS (bottom + i, middle + i);
            }

            /* Exclude the moved top segment from further swapping.  */
            bottom += len;
        }
    }

    /* Update records for the slots the non-options now occupy.  */

    first_nonopt += (optind - last_nonopt);
    last_nonopt = optind;
}

/* Initialize the internal data when the first call is made.  */
static const char *_getopt_initialize (GETOPT_THREAD_DATA_S *td, int argc, char *const *argv, const char *optstring)
{
    /* Start processing options with ARGV-element 1 (since ARGV-element 0
    is the program name); the sequence of previously skipped
    non-option ARGV-elements is empty.  */

    UNREFERENCE_PARAM(argc);
    UNREFERENCE_PARAM(argv);

    first_nonopt = last_nonopt = optind;

    nextchar = NULL;

    /* Determine how to handle the ordering of options and nonoptions.  */

    if (optstring[0] == '-')
    {
        ordering = RETURN_IN_ORDER;
        ++optstring;
    }
    else if (optstring[0] == '+')
    {
        ordering = REQUIRE_ORDER;
        ++optstring;
    }
    else if (posixly_correct != NULL)
    {

        ordering = REQUIRE_ORDER;
    }
    else
    {

        ordering = RETURN_IN_ORDER; /* 改变默认的返回方式 */
    }

    return optstring;
}

static int _internal_check_param(GETOPT_THREAD_DATA_S *td,
                                      int argc,
                                      char *const *argv,
                                      const char** optstring,
                                      int* print_errors)
{
    if ((*optstring)[0] == ':')
    {
        *print_errors = 0;
    }

    if (argc < 1)
    {
        return -1;
    }

    if (optind == 0 || !__getopt_initialized)
    {
        if (optind == 0)
        {
            optind = 1;     /* Don't scan ARGV[0], the program name.  */
        }

        *optstring = _getopt_initialize (td, argc, argv, *optstring);
        __getopt_initialized = 1;
    }

    return 0;
}

#define NONOPTION_P (argv[optind][0] != '-' || argv[optind][1] == '\0')

static void _nextchar_next_element(GETOPT_THREAD_DATA_S *td, int argc, char *const *argv)
{
    /* Give FIRST_NONOPT & LAST_NONOPT rational values if OPTIND has been
    moved back by the user (who may also have changed the arguments).  */
    if (last_nonopt > optind)
    {
        last_nonopt = optind;
    }

    if (first_nonopt > optind)
    {
        first_nonopt = optind;
    }

    if (ordering == PERMUTE)
    {
        /* If we have just processed some options following some non-options,
        exchange them so that the options come first.  */
        if (first_nonopt != last_nonopt && last_nonopt != optind)
        {
            exchange(td, (char **) argv);
        }
        else if (last_nonopt != optind)
        {
            first_nonopt = optind;
        }

        /* Skip any additional non-options
        and extend the range of non-options previously skipped.  */
        while (optind < argc && NONOPTION_P)
        {
            optind++;
        }

        last_nonopt = optind;
    }
}

static int _internal_check_nextchar(GETOPT_THREAD_DATA_S *td, int argc, char *const *argv, const struct cli_option *longopts)
{
    if (nextchar == NULL || *nextchar == '\0')
    {
        /* Advance to the next ARGV-element.  */
        _nextchar_next_element(td, argc, argv);

        /* The special ARGV-element `--' means premature end of options.
        Skip it like a null option,
        then exchange with previous non-options as if it were an option,
        then skip everything else like a non-option.  */
        if (optind != argc && !strcmp (argv[optind], "--"))
        {
            optind++;

            if (first_nonopt != last_nonopt && last_nonopt != optind)
            {
                exchange(td, (char **) argv);
            }
            else if (first_nonopt == last_nonopt)
            {
                first_nonopt = optind;
            }

            last_nonopt = argc;
            optind = argc;
        }

        /* If we have done all the ARGV-elements, stop the scan
        and back over any non-options that we skipped and permuted.  */
        if (optind == argc)
        {
            /* Set the next-arg-index to point at the non-options
            that we previously skipped, so the caller will digest them.  */
            if (first_nonopt != last_nonopt)
            {
                optind = first_nonopt;
            }

            return -1;
        }

        /* If we have come to a non-option and did not permute it,
        either stop the scan or describe it to the caller and pass it by.  */
        if (NONOPTION_P)
        {
            if (ordering == REQUIRE_ORDER)
            {
                return -1;
            }

            optarg = argv[optind++];
            return 1;
        }

        /* We have found another option-ARGV-element.
        Skip the initial punctuation.  */
        nextchar = (argv[optind] + 1 + ((longopts != NULL && argv[optind][1] == '-') ? 1 : 0));
    }

    return 0;
}

static int _argv_match(GETOPT_THREAD_DATA_S *td,
                          char *const *argv,
                          const struct cli_option *longopts,
                          int long_only,
                          int* option_index,
                          char *nameend,
                          int* indfound,
                          const struct cli_option **pfound,
                          int print_errors)
{
    const struct cli_option *p;
    int ambig = 0;
    int exact = 0;

    /* Test all long options for either exact match or abbreviated matches.  */
    for (p = longopts, *option_index = 0; p->name; p++, (*option_index)++)
    {
        if (!strncmp (p->name, nextchar, (size_t)(nameend - nextchar)))
        {
            if ((unsigned int)(nameend - nextchar) == (unsigned int)strlen(p->name))
            {
                /* Exact match found.  */
                *pfound = p;
                *indfound = *option_index;
                exact = 1;
                break;
            }
            else if (*pfound == NULL)
            {
                /* First nonexact match found.  */
                *pfound = p;
                *indfound = *option_index;
            }
            else if (long_only
                  || (*pfound)->has_arg != p->has_arg
                  || (*pfound)->flag != p->flag
                  || (*pfound)->val != p->val)
            {
                /* Second or later nonexact match found.  */
                ambig = 1;
            }
        }
    }

    if (ambig && !exact)
    {
        if (print_errors)
        {
            (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: option `%s' is ambiguous", argv[0], argv[optind]);
        }

        nextchar += strlen(nextchar);
        optind++;
        optopt = 0;

        return '?';
    }

    return 0;
}

static int _found_nameend_print(GETOPT_THREAD_DATA_S *td,
                                      char *const *argv,
                                      int print_errors,
                                      const struct cli_option *pfound,
                                      char *nameend)
{
    /* Don't test has_arg with >, because some C compilers don't
    allow it to be used on enums.  */
    if (pfound->has_arg)
    {
        optarg = nameend + 1;
    }
    else
    {
        if (print_errors)
        {
            if (argv[optind - 1][1] == '-')
            {
                /* --option */
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1,
                "Error %s: option `--%s' doesn't allow an argument",
                argv[0], pfound->name);
            }
            else
            {
                /* +option or -option */
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1,
                "Error %s: option `%c%s' doesn't allow an argument",
                argv[0], argv[optind - 1][0], pfound->name);
            }
        }

        nextchar += strlen (nextchar);

        optopt = pfound->val;
        return '?';
    }

    return 0;
}

static int _found_has_arg_print(GETOPT_THREAD_DATA_S *td,
                                     int argc,
                                     char *const *argv,
                                     const char *optstring,
                                     int print_errors,
                                     const struct cli_option *pfound)
{
    if (optind < argc)
    {
        optarg = argv[optind++];
    }
    else
    {
        if (print_errors)
        {
            (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1,
                "Error %s: option `%s' requires an argument",
                argv[0], argv[optind - 1]);
        }

        nextchar += strlen (nextchar);
        optopt = pfound->val;
        return optstring[0] == ':' ? ':' : '?';
    }

    return 0;
}

static int _argv_found(GETOPT_THREAD_DATA_S *td,
                          int argc,
                          char *const *argv,
                          const char *optstring,
                          int *longind,
                          int print_errors,
                          const struct cli_option *pfound,
                          char *nameend,
                          int option_index,
                          int indfound,
                          int* returnflag)
{
    int ret = 0;

    *returnflag = 0;

    if (pfound != NULL)
    {
        *returnflag = 1;
        option_index = indfound;
        optind++;

        if (*nameend)
        {
            ret = _found_nameend_print(td, argv, print_errors, pfound, nameend);
            if (ret != 0)
            {
                return ret;
            }
        }
        else if (pfound->has_arg == 1)
        {
            ret = _found_has_arg_print(td, argc, argv, optstring, print_errors, pfound);
            if (ret != 0)
            {
                return ret;
            }
        }

        nextchar += strlen (nextchar);

        if (longind != NULL)
        {
            *longind = option_index;
        }

        if (pfound->flag)
        {
            *(pfound->flag) = pfound->val;
            return 0;
        }

        return pfound->val;
    }

    return 0;
}

static int _argv_not_find_long_option(GETOPT_THREAD_DATA_S *td,
                                            char *const *argv,
                                            const char *optstring,
                                            int long_only,
                                            int print_errors)
{
    if (!long_only || argv[optind][1] == '-'
            || my_index (optstring, *nextchar) == NULL)
    {
        if (print_errors)
        {
            if (argv[optind][1] == '-')
            {
                /* --option */
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: unrecognized option `--%s'",
                argv[0], nextchar);
            }
            else
            {
                /* +option or -option */
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: unrecognized option `%c%s'",
                argv[0], argv[optind][0], nextchar);
            }
        }

        nextchar = (char *) "";
        optind++;
        optopt = 0;

        return '?';
    }

    return 0;
}

static int _internal_check_argv(GETOPT_THREAD_DATA_S *td,
                                    int argc,
                                    char *const *argv,
                                    const char *optstring,
                                    const struct cli_option *longopts,
                                    int *longind,
                                    int long_only,
                                    int print_errors,
                                    int* returnflag)
{
    *returnflag = 0;

    if (longopts != NULL
        && (argv[optind][1] == '-'
        || (long_only && (argv[optind][2] || !my_index (optstring, argv[optind][1])))))
    {
        char *nameend;
        const struct cli_option *pfound = NULL;
        int indfound = -1;
        int option_index = 0;
        int ret = 0;

        for (nameend = nextchar; *nameend && *nameend != '='; nameend++)
            /* Do nothing.  */ ;
        
        ret = _argv_match(td, argv, longopts, long_only, &option_index, nameend, &indfound, &pfound, print_errors);
        if (ret != 0)
        {
            *returnflag = 1;
            return ret;
        }

        ret = _argv_found(td, argc, argv, optstring, longind, print_errors, pfound, nameend, option_index, indfound, returnflag);
        if (*returnflag == 1)
        {
            return ret;
        }

        /* Can't find it as a long option.  If this is not getopt_long_only,
        or the option starts with '--' or is not a valid short
        option, then it's an error.
        Otherwise interpret it as a short option.  */
        ret = _argv_not_find_long_option(td, argv, optstring, long_only, print_errors);
        if (ret != 0)
        {
            *returnflag = 1;
            return ret;
        }
    }

    return 0;
}

static int _internal_next_char_is_null(GETOPT_THREAD_DATA_S *td,
                                           char *const *argv,
                                           int print_errors,
                                           char *temp,
                                           char c)
{
    /* Increment `optind' when we start to process its last character.  */
    if (*nextchar == '\0')
    {
        ++optind;
    }

    if (temp == NULL || c == ':')
    {
        if (print_errors)
        {
            if (posixly_correct)
            {
                /* 1003.2 specifies the format of this message.  */
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: illegal option -- %c",
                argv[0], c);
            }
            else
            {
                (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: invalid option -- %c",
                argv[0], c);
            }
        }

        optopt = c;

        return '?';
    }

    return 0;
}

static int _char_is_W_check_param(GETOPT_THREAD_DATA_S *td,
                                         int argc,
                                         char *const *argv,
                                         const char *optstring,
                                         int print_errors,
                                         char* c)
{
    /* This is an option that requires an argument.  */
    if (*nextchar != '\0')
    {
        optarg = nextchar;
        /* If we end this ARGV-element by taking the rest as an arg,
        we must advance to the next element now.  */
        optind++;
    }
    else if (optind == argc)
    {
        if (print_errors)
        {
            /* 1003.2 specifies the format of this message.  */
            (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: option requires an argument -- %c",
                argv[0], *c);
        }

        optopt = *c;

        if (optstring[0] == ':')
        {
            *c = ':';
        }
        else
        {
            *c = '?';
        }

        return *c;
    }
    else
    {
        /* We already incremented `optind' once;
        increment it again when taking next ARGV-elt as argument.  */
        optarg = argv[optind++];
    }

    return 0;
}

static int _char_is_W_match(GETOPT_THREAD_DATA_S *td,
                                  char *const *argv,
                                  const struct cli_option *longopts,
                                  int* option_index,
                                  char *nameend,
                                  int* indfound,
                                  const struct cli_option **pfound,
                                  int print_errors)
{
    const struct cli_option *p;
    int ambig = 0;
    int exact = 0;

    for (p = longopts, *option_index = 0; p && p->name; p++, (*option_index)++)
    {
        if (!strncmp (p->name, nextchar, (size_t)(nameend - nextchar)))
        {
            if ((unsigned int) (nameend - nextchar) == strlen (p->name))
            {
                /* Exact match found.  */
                *pfound = p;
                *indfound = *option_index;
                exact = 1;
                break;
            }
            else if (*pfound == NULL)
            {
                /* First nonexact match found.  */
                *pfound = p;
                *indfound = *option_index;
            }
            else
            {
                /* Second or later nonexact match found.  */
                ambig = 1;
            }
        }
    }

    if (ambig && !exact)
    {
        if (print_errors)
        {
            (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: option `-W %s' is ambiguous", argv[0], argv[optind]);
        }

        nextchar += strlen (nextchar);
        optind++;

        return '?';
    }

    return 0;
}

static int _char_is_W_found(GETOPT_THREAD_DATA_S *td,
                                 int argc,
                                 char *const *argv,
                                 const char *optstring,
                                 int *longind,
                                 int print_errors,
                                 const struct cli_option *pfound,
                                 char *nameend,
                                 int option_index,
                                 int indfound,
                                 int* returnflag)
{
    *returnflag = 0;

    if (pfound != NULL)
    {
        *returnflag = 1;
        option_index = indfound;

        if (*nameend)
        {
            /* Don't test has_arg with >, because some C compilers don't
            allow it to be used on enums.  */
            if (pfound->has_arg)
            {
                optarg = nameend + 1;
            }
            else
            {
                if (print_errors)
                {
                    (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1, "Error %s: option `-W %s' doesn't allow an argument",
                    argv[0], pfound->name);
                }

                nextchar += strlen (nextchar);
                return '?';
            }
        }
        else if (pfound->has_arg == 1)
        {
            if (optind < argc)
            {
                optarg = argv[optind++];
            }
            else
            {
                if (print_errors)
                {
                   (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1,
                    "Error %s: option `%s' requires an argument",
                    argv[0], argv[optind - 1]);
                }

                nextchar += strlen (nextchar);
                return optstring[0] == ':' ? ':' : '?';
            }
        }

        nextchar += strlen (nextchar);

        if (longind != NULL)
        {
            *longind = option_index;
        }

        if (pfound->flag)
        {
            *(pfound->flag) = pfound->val;
            return 0;
        }

        return pfound->val;
    }

    return 0;
}

static int _internal_next_char_is_W(GETOPT_THREAD_DATA_S *td,
                                         int argc,
                                         char *const *argv,
                                         const char *optstring,
                                         const struct cli_option *longopts,
                                         int *longind,
                                         int long_only,
                                         int print_errors,
                                         char *temp,
                                         char* c,
                                         int* returnflag)
{
    *returnflag = 0;
    
    UNREFERENCE_PARAM(long_only);

    if (temp[0] == 'W' && temp[1] == ';')
    {
        char *nameend;
        const struct cli_option *pfound = NULL;
        int indfound = 0;
        int option_index;
        int ret = 0;

        ret = _char_is_W_check_param(td, argc, argv, optstring, print_errors, c);
        if (ret != 0)
        {
            *returnflag = 1;
            return ret;
        }

        /* optarg is now the argument, see if it's in the
        table of longopts.  */
        for (nextchar = nameend = optarg; *nameend && *nameend != '='; nameend++)
            /* Do nothing.  */ ;

        /* Test all long options for either exact match
        or abbreviated matches.  */
        ret = _char_is_W_match(td, argv, longopts, &option_index, nameend, &indfound, &pfound, print_errors);
        if (ret != 0)
        {
            *returnflag = 1;
            return ret;
        }

        ret = _char_is_W_found(td, argc, argv, optstring, longind, print_errors, pfound, nameend, option_index, indfound, returnflag);
        if (*returnflag == 1)
        {
            return ret;
        }

        *returnflag = 1;
        nextchar = NULL;
        return 'W'; /* Let the application handle it.   */
    }

    return 0;
}

static void _internal_next_char_is_colcon(GETOPT_THREAD_DATA_S *td,
                                                int argc,
                                                char *const *argv,
                                                const char *optstring,
                                                int print_errors,
                                                char *temp,
                                                char* c)
{
    if (temp[1] == ':')
    {
        if (temp[2] == ':')
        {
            /* This is an option that accepts an argument optionally.  */
            if (*nextchar != '\0')
            {
                optarg = nextchar;
                optind++;
            }
            else
            {
                optarg = NULL;
            }

            nextchar = NULL;
        }
        else
        {
            /* This is an option that requires an argument.  */
            if (*nextchar != '\0')
            {
                optarg = nextchar;
                /* If we end this ARGV-element by taking the rest as an arg,
                we must advance to the next element now.  */
                optind++;
            }
            else if (optind == argc)
            {
                if (print_errors)
                {
                    /* 1003.2 specifies the format of this message.  */
                    (void)snprintf(td->errmsg, sizeof(td->errmsg) - 1,
                        "Error %s: option requires an argument -- %c",
                        argv[0], *c);
                }

                optopt = *c;
                if (optstring[0] == ':')
                {
                    *c = ':';
                }
                else
                {
                    *c = '?';
                }
            }
            else
            {
                /* We already incremented `optind' once;
                increment it again when taking next ARGV-elt as argument.  */
                optarg = argv[optind++];
            }

            nextchar = NULL;
        }
    }
    else
    {
        if(*nextchar != '\0')
        {         
            optopt = *c;
            *c = '?';
            optind++;
        }
        nextchar = NULL;
    }
}

int _getopt_internal_diag(GETOPT_THREAD_DATA_S *td,
                 int argc,
                 char *const *argv,
                 const char *optstring,
                 const struct cli_option *longopts,
                 int *longind,
                 int long_only)
{
    int print_errors = 1;
    int ret = 0;
    int returnflag = 0;

    optarg = NULL;

    ret = _internal_check_param(td, argc, argv, &optstring, &print_errors);
    if (ret != 0)
    {
        return -1;
    }

    /* Test whether ARGV[optind] points to a non-option argument.
    Either it does not have option syntax, or there is an environment flag
    from the shell indicating it is not an option.  The later information
    is only used when the used in the GNU libc.  */
    ret = _internal_check_nextchar(td, argc, argv, longopts);
    if (ret != 0)
    {
        return ret;
    }

    /* Decode the current option-ARGV-element.  */

    /* Check whether the ARGV-element is a long option.

    If long_only and the ARGV-element has the form "-f", where f is
    a valid short option, don't consider it an abbreviated form of
    a long option that starts with f.  Otherwise there would be no
    way to give the -f short option.

    On the other hand, if there's a long option "fubar" and
    the ARGV-element is "-fu", do consider that an abbreviation of
    the long option, just like "--fu", and not "-f" with arg "u".

    This distinction seems to be the most useful approach.  */
    ret = _internal_check_argv(td, argc, argv, optstring, longopts, longind, long_only, print_errors, &returnflag);
    if (returnflag == 1)
    {
        return ret;
    }

    /* Look at and handle the next short option-character.  */
    {
        char c = *nextchar++;
        char *temp = my_index(optstring, c);

        ret = _internal_next_char_is_null(td, argv, print_errors, temp, c);
        if (ret != 0)
        {
            return ret;
        }

        /* Convenience. Treat POSIX -W foo same as long option --foo */
        ret = _internal_next_char_is_W(td, argc, argv, optstring, longopts, longind, long_only, print_errors, temp, &c, &returnflag);
        if (returnflag == 1)
        {
            return ret;
        }

        _internal_next_char_is_colcon(td, argc, argv, optstring, print_errors, temp, &c);

        return c;
    }
}

int open_getopt(GETOPT_THREAD_DATA_S *td, int argc, char *const *argv, const char *optstring)
{
    return _getopt_internal_diag(td, argc, argv, optstring,
        (const struct cli_option *) 0,
        (int *) 0,
        0);
}


