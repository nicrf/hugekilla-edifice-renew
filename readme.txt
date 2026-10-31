Edifice

Programmed by Nelson Planting (concise@gmail.com)
Models/Graphics by John Thomas


Building:

This is tested with visual studio 2005.  It was developed in visual studio 6, and should still compile there, but appears to work just fine in 2005, so I would not recommend maintaining the old project.  The debugging features of visual studio were absolutely essential to the development, including edit and continue.  All of these features appear to work in visual studio 2005.

To build, open hugekilla.sln, IMPORTANT: set your debug directory for the project to hugekilla, and run!  If the text in the game is opaque white, you have not set your debug working directory correctly! (there's no way to store this in visual studio)  It builds in about 3 seconds for me.  If it fails to find "dsound.h", get the DirectX API or comment/stub out most of sound.cpp (there are stubs in a comment block at the bottom).  That should be the only dependency.

See hugekilla/read_me_first.html for instructions on playing the game.  If you would like to play in sandbox mode, which allows infinite building, open the console (~), type gamemode 1 (and press enter).


About the program:

This should not be an example of great coding practices.  The whole thing is very simple, with the exception of the land rendering and the socket code.  Coding was begun after one year of my learning c++.  The graphics are the result of tons of tweaking and not any amazing techniques.

The game was a learning experience for me, especially in code cleanliness, so please excuse the occasional piece of crap.  I can help deciphering it if it's needed.

I have cleaned up a few extra files, and also changed some of the old constants to match newer hardware - increased the size of the land and the draw distance.  Other than that, I don't currently plan to maintain this.  I wrote it 10 years ago and don't know what I was thinking :)


Why people might be interested in this:

It's an example of how to use a lot of APIs:  UDP, OpenGL, Win32, DirectSound.  It's also an example of a simple video game.

It would be possible to factor out pieces of this and make new projects.  The socket class, for instance, could be a great low-level tool usable across many games, since it uses only UDP and has reliable transport built in.

The "gameplay" aspects of this were never really developed.  It would be cool to see this improved.


Why LGPL?:

I would like to see bits and pieces of this factored out and placed into many other projects.  I would like to allow static linking of this code without the "viral" aspect of the GPL.

My hope is that people will factor out and improve pieces, then contribute them back to the open-source community.  I have little motivation to improve this - it was 10 years ago, and I've made better tools to use since then.


Finally...:

Have fun with the code.  I had a lot of fun writing it.
