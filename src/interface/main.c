/*****************************************************************************
 * main.c: main vlc source
 * Includes the main() function for vlc. Parses command line, start interface
 * and spawn threads.
 *****************************************************************************
 * Copyright (C) 1998, 1999, 2000 VideoLAN
 * $Id: main.c,v 1.94 2001/05/14 15:58:04 reno Exp $
 *
 * Authors: Vincent Seguin <seguin@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include "defs.h"

#include <signal.h>                               /* SIGHUP, SIGINT, SIGKILL */
#include <stdio.h>                                              /* sprintf() */

#ifdef HAVE_GETOPT_LONG
#   ifdef HAVE_GETOPT_H
#       include <getopt.h>                                       /* getopt() */
#   endif
#else
#   include "GNUgetopt/getopt.h"
#endif

#ifdef SYS_DARWIN1_3
#   include <mach/mach.h>                               /* Altivec detection */
#   include <mach/mach_error.h>       /* some day the header files||compiler *
                                                       will define it for us */
#   include <mach/bootstrap.h>
#endif

#include <unistd.h>
#include <errno.h>                                                 /* ENOMEM */
#include <stdlib.h>                                  /* getenv(), strtol(),  */
#include <string.h>                                            /* strerror() */

#include "config.h"
#include "common.h"
#include "debug.h"
#include "threads.h"
#include "mtime.h"
#include "tests.h"                                              /* TestCPU() */
#include "modules.h"

#include "stream_control.h"
#include "input_ext-intf.h"

#include "intf_msg.h"
#include "intf_playlist.h"
#include "interface.h"

#include "audio_output.h"

#include "video.h"
#include "video_output.h"

#ifdef SYS_BEOS
#   include "beos_specific.h"
#endif

#ifdef SYS_DARWIN1_3
#   include "darwin_specific.h"
#endif

#include "netutils.h"                                 /* network_ChannelJoin */

#include "main.h"

/*****************************************************************************
 * Command line options constants. If something is changed here, be sure that
 * GetConfiguration and Usage are also changed.
 *****************************************************************************/

/* Long options return values - note that values corresponding to short options
 * chars, and in general any regular char, should be avoided */
#define OPT_NOAUDIO             150
#define OPT_STEREO              151
#define OPT_MONO                152
#define OPT_SPDIF               153

#define OPT_NOVIDEO             160
#define OPT_DISPLAY             161
#define OPT_WIDTH               162
#define OPT_HEIGHT              163
#define OPT_COLOR               164
#define OPT_FULLSCREEN          165
#define OPT_OVERLAY             166

#define OPT_CHANNELS            170
#define OPT_SERVER              171
#define OPT_PORT                172
#define OPT_BROADCAST           173

#define OPT_INPUT               180
#define OPT_MOTION              181
#define OPT_IDCT                182
#define OPT_YUV                 183

#define OPT_SYNCHRO             190
#define OPT_WARNING             191
#define OPT_VERSION             192

/* Usage fashion */
#define USAGE                     0
#define SHORT_HELP                1
#define LONG_HELP                 2

/* Long options */
static const struct option longopts[] =
{
    /*  name,               has_arg,    flag,   val */

    /* General/common options */
    {   "help",             0,          0,      'h' },
    {   "longhelp",         0,          0,      'H' },
    {   "version",          0,          0,      OPT_VERSION },

    /* Interface options */
    {   "intf",             1,          0,      'I' },
    {   "warning",          1,          0,      OPT_WARNING },

    /* Audio options */
    {   "noaudio",          0,          0,      OPT_NOAUDIO },
    {   "aout",             1,          0,      'A' },
    {   "stereo",           0,          0,      OPT_STEREO },
    {   "mono",             0,          0,      OPT_MONO },
    {   "spdif",            0,          0,      OPT_SPDIF },

    /* Video options */
    {   "novideo",          0,          0,      OPT_NOVIDEO },
    {   "vout",             1,          0,      'V' },
    {   "display",          1,          0,      OPT_DISPLAY },
    {   "width",            1,          0,      OPT_WIDTH },
    {   "height",           1,          0,      OPT_HEIGHT },
    {   "grayscale",        0,          0,      'g' },
    {   "color",            0,          0,      OPT_COLOR },
    {   "motion",           1,          0,      OPT_MOTION },
    {   "idct",             1,          0,      OPT_IDCT },
    {   "yuv",              1,          0,      OPT_YUV },
    {   "fullscreen",       0,          0,      OPT_FULLSCREEN },
    {   "overlay",          0,          0,      OPT_OVERLAY },

    /* DVD options */
    {   "dvdtitle",         1,          0,      't' },
    {   "dvdchapter",       1,          0,      'T' },
    {   "dvdangle",         1,          0,      'u' },
    {   "dvdaudio",         1,          0,      'a' },
    {   "dvdchannel",       1,          0,      'c' },
    {   "dvdsubtitle",      1,          0,      's' },
    
    /* Input options */
    {   "input",            1,          0,      OPT_INPUT },
    {   "channels",         0,          0,      OPT_CHANNELS },
    {   "server",           1,          0,      OPT_SERVER },
    {   "port",             1,          0,      OPT_PORT },
    {   "broadcast",        1,          0,      OPT_BROADCAST },

    /* Synchro options */
    {   "synchro",          1,          0,      OPT_SYNCHRO },
    {   0,                  0,          0,      0 }
};

/* Short options */
static const char *psz_shortopts = "hHvgt:T:u:a:s:c:I:A:V:";

/*****************************************************************************
 * Global variable program_data - these are the only ones, see main.h and
 * modules.h
 *****************************************************************************/
main_t        *p_main;
module_bank_t *p_module_bank;
aout_bank_t   *p_aout_bank;
vout_bank_t   *p_vout_bank;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  GetConfiguration        ( int *pi_argc, char *ppsz_argv[],
                                      char *ppsz_env[] );
static int  GetFilenames            ( int i_argc, char *ppsz_argv[] );
static void Usage                   ( int i_fashion );
static void Version                 ( void );

static void InitSignalHandler       ( void );
static void SimpleSignalHandler     ( int i_signal );
static void FatalSignalHandler      ( int i_signal );

static int  CPUCapabilities         ( void );

/*****************************************************************************
 * main: parse command line, start interface and spawn threads
 *****************************************************************************
 * Steps during program execution are:
 *      -configuration parsing and messages interface initialization
 *      -opening of audio output device and some global modules
 *      -execution of interface, which exit on error or on user request
 *      -closing of audio output device and some global modules
 * On error, the spawned threads are canceled, and the open devices closed.
 *****************************************************************************/
int main( int i_argc, char *ppsz_argv[], char *ppsz_env[] )
{
    main_t        main_data;                /* root of all data - see main.h */
    module_bank_t module_bank;
    aout_bank_t   aout_bank;
    vout_bank_t   vout_bank;

    p_main        = &main_data;               /* set up the global variables */
    p_module_bank = &module_bank;
    p_aout_bank   = &aout_bank;
    p_vout_bank   = &vout_bank;

    /*
     * Test if our code is likely to run on this CPU 
     */
    p_main->i_cpu_capabilities = CPUCapabilities();
    
#if defined( __pentium__ ) || defined( __pentiumpro__ )
    if( ! TestCPU( CPU_CAPABILITY_586 ) )
    {
        fprintf( stderr, "error: this program needs a Pentium CPU,\n"
                         "please try a version without Pentium support\n" );
        return( 1 );
    }
#endif

    /*
     * System specific initialization code
     */
#if defined( SYS_BEOS ) || defined( SYS_DARWIN1_3 )
    system_Init( &i_argc, ppsz_argv, ppsz_env );
#endif

    /*
     * Initialize messages interface
     */
    p_main->p_msg = intf_MsgCreate();
    if( !p_main->p_msg )                         /* start messages interface */
    {
        fprintf( stderr, "error: can't initialize messages interface (%s)\n",
                 strerror(errno) );
        return( errno );
    }

    intf_MsgImm( COPYRIGHT_MESSAGE );

    /*
     * Read configuration
     */
    if( GetConfiguration( &i_argc, ppsz_argv, ppsz_env ) ) /* parse cmd line */
    {
        intf_MsgDestroy();
        return( errno );
    }

    /*
     * Initialize playlist and get commandline files
     */
    p_main->p_playlist = intf_PlaylistCreate();
    if( !p_main->p_playlist )
    {
        intf_ErrMsg( "playlist error: playlist initialization failed" );
        intf_MsgDestroy();
        return( errno );
    }
    intf_PlaylistInit( p_main->p_playlist );

    /*
     * Get input filenames given as commandline arguments
     */
    GetFilenames( i_argc, ppsz_argv );

    /*
     * Initialize module, aout and vout banks
     */
    module_InitBank();
    aout_InitBank();
    vout_InitBank();

    /*
     * Initialize shared resources and libraries
     */
    if( p_main->b_channels && network_ChannelCreate() )
    {
        /* On error during Channels initialization, switch off channels */
        intf_Msg( "Channels initialization failed : "
                  "Channel management is deactivated" );
        p_main->b_channels = 0;
    }

    /*
     * Try to run the interface
     */
    p_main->p_intf = intf_Create();
    if( p_main->p_intf == NULL )
    {
        intf_ErrMsg( "intf error: interface initialization failed" );
    }
    else
    {
        /*
         * Set signal handling policy for all threads
         */
        InitSignalHandler();

        /*
         * This is the main loop
         */
        p_main->p_intf->pf_run( p_main->p_intf );

        /*
         * Finished, destroy the interface
         */
        intf_Destroy( p_main->p_intf );

        /*
         * Go back into channel 0 which is the network
         */
        if( p_main->b_channels )
        {
            network_ChannelJoin( COMMON_CHANNEL );
        }
    }

    /*
     * Free module, aout and vout banks
     */
    vout_EndBank();
    aout_EndBank();
    module_EndBank();

    /*
     * Free playlist
     */
    intf_PlaylistDestroy( p_main->p_playlist );

    /*
     * System specific cleaning code
     */
#if defined( SYS_BEOS ) || defined( SYS_DARWIN1_3 )
    system_End();
#endif

    /*
     * Terminate messages interface and program
     */
    intf_Msg( "intf: program terminated" );
    intf_MsgDestroy();

    return 0;
}

/*****************************************************************************
 * main_GetIntVariable: get the int value of an environment variable
 *****************************************************************************
 * This function is used to read some default parameters in modules.
 *****************************************************************************/
int main_GetIntVariable( char *psz_name, int i_default )
{
    char *      psz_env;                                /* environment value */
    char *      psz_end;                             /* end of parsing index */
    long int    i_value;                                            /* value */

    psz_env = getenv( psz_name );
    if( psz_env )
    {
        i_value = strtol( psz_env, &psz_end, 0 );
        if( (*psz_env != '\0') && (*psz_end == '\0') )
        {
            return( i_value );
        }
    }
    return( i_default );
}

/*****************************************************************************
 * main_GetPszVariable: get the string value of an environment variable
 *****************************************************************************
 * This function is used to read some default parameters in modules.
 *****************************************************************************/
char * main_GetPszVariable( char *psz_name, char *psz_default )
{
    char *psz_env;

    psz_env = getenv( psz_name );
    if( psz_env )
    {
        return( psz_env );
    }
    return( psz_default );
}

/*****************************************************************************
 * main_PutPszVariable: set the string value of an environment variable
 *****************************************************************************
 * This function is used to set some default parameters in modules. The use of
 * this function will cause some memory leak: since some systems use the pointer
 * passed to putenv to store the environment string, it can't be freed.
 *****************************************************************************/
void main_PutPszVariable( char *psz_name, char *psz_value )
{
    char *psz_env;

    psz_env = malloc( strlen(psz_name) + strlen(psz_value) + 2 );
    if( psz_env == NULL )
    {
        intf_ErrMsg( "intf error: cannot create psz_env (%s)",
                     strerror(ENOMEM) );
    }
    else
    {
        sprintf( psz_env, "%s=%s", psz_name, psz_value );
        if( putenv( psz_env ) )
        {
            intf_ErrMsg( "intf error: cannot putenv (%s)", strerror(errno) );
        }
    }
}

/*****************************************************************************
 * main_PutIntVariable: set the integer value of an environment variable
 *****************************************************************************
 * This function is used to set some default parameters in modules. The use of
 * this function will cause some memory leak: since some systems use the pointer
 * passed to putenv to store the environment string, it can't be freed.
 *****************************************************************************/
void main_PutIntVariable( char *psz_name, int i_value )
{
    char psz_value[ 256 ];                               /* buffer for value */

    sprintf( psz_value, "%d", i_value );
    main_PutPszVariable( psz_name, psz_value );
}

/* following functions are local */

/*****************************************************************************
 * GetConfiguration: parse command line
 *****************************************************************************
 * Parse command line and configuration file for configuration. If the inline
 * help is requested, the function Usage() is called and the function returns
 * -1 (causing main() to exit). The messages interface is initialized at this
 * stage, but most structures are not allocated, so only environment should
 * be used.
 *****************************************************************************/
static int GetConfiguration( int *pi_argc, char *ppsz_argv[], char *ppsz_env[] )
{
    int   i_cmd;
    char *p_tmp;

    /* Set default configuration and copy arguments */
    p_main->i_argc    = *pi_argc;
    p_main->ppsz_argv = ppsz_argv;
    p_main->ppsz_env  = ppsz_env;

    p_main->b_audio     = 1;
    p_main->b_video     = 1;
    p_main->b_channels  = 0;

    p_main->i_warning_level = 0;

    /* Get the executable name (similar to the basename command) */
    p_main->psz_arg0 = p_tmp = ppsz_argv[ 0 ];
    while( *p_tmp )
    {
        if( *p_tmp == '/' )
        {
            p_main->psz_arg0 = ++p_tmp;
        }
        else
        {
            ++p_tmp;
        }
    }

#ifdef SYS_DARWIN1_3
    /* When vlc.app is run by double clicking in Mac OS X, the 2nd arg
     * is the PSN - process serial number (a unique PID-ish thingie)
     * still ok for real Darwin & when run from command line */
    if ( (*pi_argc > 1) && (strncmp( ppsz_argv[ 1 ] , "-psn" , 4 ) == 0) )
                                        /* for example -psn_0_9306113 */
    {
        /* GDMF!... I can't do this or else the MacOSX window server will
         * not pick up the PSN and not register the app and we crash...
         * hence the following kludge otherwise we'll get confused w/ argv[1]
         * being an input file name */
#if 0
        ppsz_argv[ 1 ] = NULL;
#endif
        *pi_argc = *pi_argc - 1;
        pi_argc--;
        return( 0 );
    }
#endif

    /* Parse command line options */
    opterr = 0;
    while( ( i_cmd = getopt_long( *pi_argc, ppsz_argv,
                                   psz_shortopts, longopts, 0 ) ) != EOF )
    {
        switch( i_cmd )
        {
        /* General/common options */
        case 'h':                                              /* -h, --help */
            Usage( SHORT_HELP );
            return( -1 );
            break;
        case 'H':                                          /* -H, --longhelp */
            Usage( LONG_HELP );
            return( -1 );
            break;
        case OPT_VERSION:                                       /* --version */
            Version();
            return( -1 );
            break;
        case 'v':                                           /* -v, --verbose */
            p_main->i_warning_level++;
            break;

        /* Interface warning messages level */
        case 'I':                                              /* -I, --intf */
            main_PutPszVariable( INTF_METHOD_VAR, optarg );
            break;
        case OPT_WARNING:                                       /* --warning */
            intf_ErrMsg( "intf error: `--warning' is deprecated, use `-v'" );
            p_main->i_warning_level = atoi(optarg);
            break;

        /* Audio options */
        case OPT_NOAUDIO:                                       /* --noaudio */
            p_main->b_audio = 0;
            break;
        case 'A':                                              /* -A, --aout */
            main_PutPszVariable( AOUT_METHOD_VAR, optarg );
            break;
        case OPT_STEREO:                                         /* --stereo */
            main_PutIntVariable( AOUT_STEREO_VAR, 1 );
            break;
        case OPT_MONO:                                             /* --mono */
            main_PutIntVariable( AOUT_STEREO_VAR, 0 );
            break;
        case OPT_SPDIF:                                           /* --spdif */
            main_PutIntVariable( AOUT_SPDIF_VAR, 1 );
            break;

        /* Video options */
        case OPT_NOVIDEO:                                       /* --novideo */
            p_main->b_video = 0;
            break;
        case 'V':                                              /* -V, --vout */
            main_PutPszVariable( VOUT_METHOD_VAR, optarg );
            break;
        case OPT_DISPLAY:                                       /* --display */
            main_PutPszVariable( VOUT_DISPLAY_VAR, optarg );
            break;
        case OPT_WIDTH:                                           /* --width */
            main_PutPszVariable( VOUT_WIDTH_VAR, optarg );
            break;
        case OPT_HEIGHT:                                         /* --height */
            main_PutPszVariable( VOUT_HEIGHT_VAR, optarg );
            break;
        case 'g':                                         /* -g, --grayscale */
            main_PutIntVariable( VOUT_GRAYSCALE_VAR, 1 );
            break;
        case OPT_COLOR:                                           /* --color */
            main_PutIntVariable( VOUT_GRAYSCALE_VAR, 0 );
            break;
        case OPT_FULLSCREEN:                                 /* --fullscreen */
            main_PutIntVariable( VOUT_FULLSCREEN_VAR, 1 );
            break;
        case OPT_OVERLAY:                                       /* --overlay */
            main_PutIntVariable( VOUT_OVERLAY_VAR, 1 );
            break;
        case OPT_MOTION:                                         /* --motion */
            main_PutPszVariable( MOTION_METHOD_VAR, optarg );
            break;
        case OPT_IDCT:                                             /* --idct */
            main_PutPszVariable( IDCT_METHOD_VAR, optarg );
            break;
        case OPT_YUV:                                               /* --yuv */
            main_PutPszVariable( YUV_METHOD_VAR, optarg );
            break;

        /* DVD options */
        case 't':
            main_PutIntVariable( INPUT_TITLE_VAR, atoi(optarg) );
            break;
        case 'T':
            main_PutIntVariable( INPUT_CHAPTER_VAR, atoi(optarg) );
            break;
        case 'u':
            main_PutIntVariable( INPUT_ANGLE_VAR, atoi(optarg) );
            break;
        case 'a':
            if ( ! strcmp(optarg, "ac3") )
                main_PutIntVariable( INPUT_AUDIO_VAR, REQUESTED_AC3 );
            else if ( ! strcmp(optarg, "lpcm") )
                main_PutIntVariable( INPUT_AUDIO_VAR, REQUESTED_LPCM );
            else if ( ! strcmp(optarg, "mpeg") )
                main_PutIntVariable( INPUT_AUDIO_VAR, REQUESTED_MPEG );
            else
                main_PutIntVariable( INPUT_AUDIO_VAR, REQUESTED_NOAUDIO );
            break;
        case 'c':
            main_PutIntVariable( INPUT_CHANNEL_VAR, atoi(optarg) );
            break;
        case 's':
            main_PutIntVariable( INPUT_SUBTITLE_VAR, atoi(optarg) );
            break;

        /* Input options */
        case OPT_INPUT:                                           /* --input */
            main_PutPszVariable( INPUT_METHOD_VAR, optarg );
            break;
        case OPT_CHANNELS:                                     /* --channels */
            p_main->b_channels = 1;
            break;
        case OPT_SERVER:                                         /* --server */
            main_PutPszVariable( INPUT_SERVER_VAR, optarg );
            break;
        case OPT_PORT:                                             /* --port */
            main_PutPszVariable( INPUT_PORT_VAR, optarg );
            break;
        case OPT_BROADCAST:                                   /* --broadcast */
            main_PutPszVariable( INPUT_BROADCAST_VAR, optarg );
            break;

        /* Synchro options */
        case OPT_SYNCHRO:                                      
            main_PutPszVariable( VPAR_SYNCHRO_VAR, optarg );
            break;
            
        /* Internal error: unknown option */
        case '?':
        default:
            intf_ErrMsg( "intf error: unknown option `%s'",
                         ppsz_argv[optind - 1] );
            Usage( USAGE );
            return( EINVAL );
            break;
        }
    }

    if( p_main->i_warning_level < 0 )
    {
        p_main->i_warning_level = 0;
    }

    return( 0 );
}

/*****************************************************************************
 * GetFilenames: parse command line options which are not flags
 *****************************************************************************
 * Parse command line for input files.
 *****************************************************************************/
static int GetFilenames( int i_argc, char *ppsz_argv[] )
{
    int i_opt;

    /* We assume that the remaining parameters are filenames */
    for( i_opt = optind; i_opt < i_argc; i_opt++ )
    {
        intf_PlaylistAdd( p_main->p_playlist, PLAYLIST_END,
                          ppsz_argv[ i_opt ] );
    }

    return( 0 );
}

/*****************************************************************************
 * Usage: print program usage
 *****************************************************************************
 * Print a short inline help. Message interface is initialized at this stage.
 *****************************************************************************/
static void Usage( int i_fashion )
{
    /* Usage */
    intf_MsgImm( "Usage: %s [options] [parameters] [file]...",
                 p_main->psz_arg0 );

    if( i_fashion == USAGE )
    {
        intf_MsgImm( "Try `%s --help' for more information.",
                     p_main->psz_arg0 );
        return;
    }

    /* Options */
    intf_MsgImm( "\nOptions:"
          "\n  -I, --intf <module>            \tinterface method"
          "\n  -v, --verbose                  \tverbose mode (cumulative)"
          "\n"
          "\n      --noaudio                  \tdisable audio"
          "\n  -A, --aout <module>            \taudio output method"
          "\n      --stereo, --mono           \tstereo/mono audio"
          "\n      --spdif                    \tAC3 pass-through mode"
          "\n"
          "\n      --novideo                  \tdisable video"
          "\n  -V, --vout <module>            \tvideo output method"
          "\n      --display <display>        \tdisplay string"
          "\n      --width <w>, --height <h>  \tdisplay dimensions"
          "\n  -g, --grayscale                \tgrayscale output"
          "\n      --fullscreen               \tfullscreen output"
          "\n      --overlay                  \taccelerated display"
          "\n      --color                    \tcolor output"
          "\n      --motion <module>          \tmotion compensation method"
          "\n      --idct <module>            \tIDCT method"
          "\n      --yuv <module>             \tYUV method"
          "\n      --synchro <type>           \tforce synchro algorithm"
          "\n"
          "\n  -t, --dvdtitle <num>           \tchoose DVD title"
          "\n  -T, --dvdchapter <num>         \tchoose DVD chapter"
          "\n  -u, --dvdangle <num>           \tchoose DVD angle"
          "\n  -a, --dvdaudio <type>          \tchoose DVD audio type"
          "\n  -c, --dvdchannel <channel>     \tchoose DVD audio channel"
          "\n  -s, --dvdsubtitle <channel>    \tchoose DVD subtitle channel"
          "\n"
          "\n      --input                    \tinput method"
          "\n      --channels                 \tenable channels"
          "\n      --server <host>            \tvideo server address"
          "\n      --port <port>              \tvideo server port"
          "\n      --broadcast                \tlisten to a broadcast"
          "\n"
          "\n  -h, --help                     \tprint help and exit"
          "\n  -H, --longhelp                 \tprint long help and exit"
          "\n      --version                  \toutput version information and exit" );

    if( i_fashion == SHORT_HELP )
        return;

    /* Interface parameters */
    intf_MsgImm( "\nInterface parameters:"
        "\n  " INTF_METHOD_VAR "=<method name>          \tinterface method"
        "\n  " INTF_INIT_SCRIPT_VAR "=<filename>               \tinitialization script"
        "\n  " INTF_CHANNELS_VAR "=<filename>            \tchannels list" );

    /* Audio parameters */
    intf_MsgImm( "\nAudio parameters:"
        "\n  " AOUT_METHOD_VAR "=<method name>        \taudio method"
        "\n  " AOUT_DSP_VAR "=<filename>              \tdsp device path"
        "\n  " AOUT_STEREO_VAR "={1|0}                \tstereo or mono output"
        "\n  " AOUT_SPDIF_VAR "={1|0}                 \tAC3 pass-through mode"
        "\n  " AOUT_RATE_VAR "=<rate>             \toutput rate" );

    /* Video parameters */
    intf_MsgImm( "\nVideo parameters:"
        "\n  " VOUT_METHOD_VAR "=<method name>        \tdisplay method"
        "\n  " VOUT_DISPLAY_VAR "=<display name>      \tdisplay used"
        "\n  " VOUT_WIDTH_VAR "=<width>               \tdisplay width"
        "\n  " VOUT_HEIGHT_VAR "=<height>             \tdislay height"
        "\n  " VOUT_FB_DEV_VAR "=<filename>           \tframebuffer device path"
        "\n  " VOUT_GRAYSCALE_VAR "={1|0}             \tgrayscale or color output"
        "\n  " VOUT_FULLSCREEN_VAR "={1|0}            \tfullscreen"
        "\n  " VOUT_OVERLAY_VAR "={1|0}               \toverlay"
        "\n  " MOTION_METHOD_VAR "=<method name>      \tmotion compensation method"
        "\n  " IDCT_METHOD_VAR "=<method name>        \tIDCT method"
        "\n  " YUV_METHOD_VAR "=<method name>         \tYUV method"
        "\n  " VPAR_SYNCHRO_VAR "={I|I+|IP|IP+|IPB}   \tsynchro algorithm" );

    /* DVD parameters */
    intf_MsgImm( "\nDVD parameters:"
        "\n  " INPUT_DVD_DEVICE_VAR "=<device>           \tDVD device"
        "\n  " INPUT_TITLE_VAR "=<title>             \ttitle number"
        "\n  " INPUT_CHAPTER_VAR "=<chapter>         \tchapter number"
        "\n  " INPUT_ANGLE_VAR "=<angle>             \tangle number"
        "\n  " INPUT_AUDIO_VAR "={ac3|lpcm|mpeg|off} \taudio type"
        "\n  " INPUT_CHANNEL_VAR "=[0-15]            \taudio channel"
        "\n  " INPUT_SUBTITLE_VAR "=[0-31]           \tsubtitle channel" );

    /* Input parameters */
    intf_MsgImm( "\nInput parameters:"
        "\n  " INPUT_SERVER_VAR "=<hostname>          \tvideo server"
        "\n  " INPUT_PORT_VAR "=<port>            \tvideo server port"
        "\n  " INPUT_IFACE_VAR "=<interface>          \tnetwork interface"
        "\n  " INPUT_BROADCAST_VAR "=<addr>            \tbroadcast mode"
        "\n  " INPUT_CHANNEL_SERVER_VAR "=<hostname>     \tchannel server"
        "\n  " INPUT_CHANNEL_PORT_VAR "=<port>         \tchannel server port" );

}

/*****************************************************************************
 * Version: print complete program version
 *****************************************************************************
 * Print complete program version and build number.
 *****************************************************************************/
static void Version( void )
{
    intf_MsgImm( VERSION_MESSAGE
        "This program comes with NO WARRANTY, to the extent permitted by law.\n"
        "You may redistribute it under the terms of the GNU General Public License;\n"
        "see the file named COPYING for details.\n"
        "Written by the VideoLAN team at Ecole Centrale, Paris." );
}

/*****************************************************************************
 * InitSignalHandler: system signal handler initialization
 *****************************************************************************
 * Set the signal handlers. SIGTERM is not intercepted, because we need at
 * at least a method to kill the program when all other methods failed, and
 * when we don't want to use SIGKILL.
 *****************************************************************************/
static void InitSignalHandler( void )
{
    /* Termination signals */
#ifndef WIN32
    signal( SIGINT,  FatalSignalHandler );
    signal( SIGHUP,  FatalSignalHandler );
    signal( SIGQUIT, FatalSignalHandler );

    /* Other signals */
    signal( SIGALRM, SimpleSignalHandler );
    signal( SIGPIPE, SimpleSignalHandler );
#endif
}


/*****************************************************************************
 * SimpleSignalHandler: system signal handler
 *****************************************************************************
 * This function is called when a non fatal signal is received by the program.
 *****************************************************************************/
static void SimpleSignalHandler( int i_signal )
{
    /* Acknowledge the signal received */
    intf_WarnMsg( 0, "intf: ignoring signal %d", i_signal );
}


/*****************************************************************************
 * FatalSignalHandler: system signal handler
 *****************************************************************************
 * This function is called when a fatal signal is received by the program.
 * It tries to end the program in a clean way.
 *****************************************************************************/
static void FatalSignalHandler( int i_signal )
{
    /* Once a signal has been trapped, the termination sequence will be
     * armed and following signals will be ignored to avoid sending messages
     * to an interface having been destroyed */
#ifndef WIN32
    signal( SIGINT,  SIG_IGN );
    signal( SIGHUP,  SIG_IGN );
    signal( SIGQUIT, SIG_IGN );
#endif

    /* Acknowledge the signal received */
    intf_ErrMsgImm( "intf error: signal %d received, exiting", i_signal );

    /* Try to terminate everything - this is done by requesting the end of the
     * interface thread */
    p_main->p_intf->b_die = 1;
}

/*****************************************************************************
 * CPUCapabilities: list the processors MMX support and other capabilities
 *****************************************************************************
 * This function is called to list extensions the CPU may have.
 *****************************************************************************/
static int CPUCapabilities( void )
{
    int i_capabilities = CPU_CAPABILITY_NONE;

#if defined( SYS_BEOS )
    i_capabilities |= CPU_CAPABILITY_486
                      | CPU_CAPABILITY_586
                      | CPU_CAPABILITY_MMX;

#elif defined( SYS_DARWIN1_3 )

    struct host_basic_info hi;
    kern_return_t          ret;
    host_name_port_t       host;

    int i_size;
    char *psz_name, *psz_subname;

    /* Should 'never' fail? */
    host = mach_host_self();

    i_size = sizeof( hi ) / sizeof( int );
    ret = host_info( host, HOST_BASIC_INFO, ( host_info_t )&hi, &i_size );

    if( ret != KERN_SUCCESS )
    {
        intf_ErrMsg( "error: couldn't get CPU information" );
        return( i_capabilities );
    }

    slot_name( hi.cpu_type, hi.cpu_subtype, &psz_name, &psz_subname );
    /* FIXME: need better way to detect newer proccessors.
     * could do strncmp(a,b,5), but that's real ugly */
    if( strcmp(psz_name, "ppc7400") || strcmp(psz_name, "ppc7450") )
    {
        i_capabilities |= CPU_CAPABILITY_ALTIVEC;
    }

#elif defined( __i386__ )
    unsigned int  i_eax, i_ebx, i_ecx, i_edx;
    boolean_t     b_amd;

#   define cpuid( a )              \
    asm volatile ( "cpuid"         \
                 : "=a" ( i_eax ), \
                   "=b" ( i_ebx ), \
                   "=c" ( i_ecx ), \
                   "=d" ( i_edx )  \
                 : "a"  ( a )      \
                 : "cc" );         \

    /* test for a 486 CPU */
    asm volatile ( "pushfl\n\t"
                   "popl %%eax\n\t"
                   "movl %%eax, %%ebx\n\t"
                   "xorl $0x200000, %%eax\n\t"
                   "pushl %%eax\n\t"
                   "popfl\n\t"
                   "pushfl\n\t"
                   "popl %%eax"
                 : "=a" ( i_eax ),
                   "=b" ( i_ebx )
                 :
                 : "cc" );

    if( i_eax == i_ebx )
    {
        return( i_capabilities );
    }

    i_capabilities |= CPU_CAPABILITY_486;

    /* the CPU supports the CPUID instruction - get its level */
    cpuid( 0x00000000 );

    if( !i_eax )
    {
        return( i_capabilities );
    }

    /* FIXME: this isn't correct, since some 486s have cpuid */
    i_capabilities |= CPU_CAPABILITY_586;

    /* borrowed from mpeg2dec */
    b_amd = ( i_ebx == 0x68747541 ) && ( i_ecx == 0x444d4163 )
                    && ( i_edx == 0x69746e65 );

    /* test for the MMX flag */
    cpuid( 0x00000001 );

    if( ! (i_edx & 0x00800000) )
    {
        return( i_capabilities );
    }

    i_capabilities |= CPU_CAPABILITY_MMX;

    if( i_edx & 0x02000000 )
    {
        i_capabilities |= CPU_CAPABILITY_MMXEXT;
        i_capabilities |= CPU_CAPABILITY_SSE;
    }
    
    /* test for additional capabilities */
    cpuid( 0x80000000 );

    if( i_eax < 0x80000001 )
    {
        return( i_capabilities );
    }

    /* list these additional capabilities */
    cpuid( 0x80000001 );

    if( i_edx & 0x80000000 )
    {
        i_capabilities |= CPU_CAPABILITY_3DNOW;
    }

    if( b_amd && ( i_edx & 0x00400000 ) )
    {
        i_capabilities |= CPU_CAPABILITY_MMXEXT;
    }
#else
    /* default behaviour */

#endif
    return( i_capabilities );
}

