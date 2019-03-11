Ogg-winmm CD Audio Emulator
---------------------------

This fork is based on the pachuco fork.
https://github.com/pachuco/ogg-winmm

I have converted the project to Visual Studio 2017.

This project (originally by Hifi) uses .ogg music files on the disk
to emulate CD tracks, replacing the need to have a CD in the drive
to play music in certain games. Good Old Games uses a modified version
for several of their games. 

It has gone unmaintained, so I took it upon myself to solve a couple issues,
namely making it work on Windows 10. Good Old Games has fixed their own version,
but it's not as useful without the source available, so I fixed it myself as well.

BUILDING:

Open the VS2017 solution "winmm.sln".
Run Nuget Restore to download the needed dependencies (vorbis and ogg).
Now build the winmm.dll in Release or Debug mode.

USAGE:

Copy "winmm.dll" into the same folder as the executable of the game you want 
to emulate CD music for.

In the same folder, make a "Music" subdirectory. Place the recorded music files
from the disk as Track02.ogg, Track03.ogg, and so on in this Music folder. Remember,
it starts with track02!

Now, instead of playing music from the CD, the game will play music from these
files instead.
