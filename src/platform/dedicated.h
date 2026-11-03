//The dedicated server (dedicated.cpp): the game's server with no window, renderer,
//audio nor input, no player of its own (every slot goes to remote players), its
//settings from net.ini and sv.ini overridden by command line flags and HK_* variables,
//its log on stdout and its console on stdin. edifice-server is only that (CMake
//EDIFICE_DEDICATED, platform_server.cpp, no SDL); the game runs it with --dedicated
//or HK_DEDICATED=1 (platform_sdl.cpp). See server/README.md and BUILD.md.

#ifndef DEDICATEDH
#define DEDICATEDH

//reads the flags (and HK_DEDICATED): 1 to run the dedicated server, 0 the game.
//always: edifice-server, which is nothing else. *exitcode>=0: exit with it right away
//(--help, --version, --healthcheck, a bad flag)
int DedicatedArgs(int argc,char **argv,int always,int *exitcode);
//once in the data folder with the user folder set: the server, until SIGINT,
//SIGTERM or "quit" on the console; the exit code
int DedicatedRun();
void DedicatedQuit();        //ends DedicatedRun's loop (HKQuit)

#endif
