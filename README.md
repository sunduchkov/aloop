Audio path-through program designed to use on [embedded ARM computers](https://github.com/sunduchkov/aloop/wiki/Audio-Boards) like Beagleboard xM, Beaglebone Black, Pandaboard ES operating on Ubuntu Linux. The program uses ALSA interface to communicate with a hardware.

The program can be built in a cross-build environment (e.g. on Oracle Virtualbox running Ubuntu). Please look to the Wiki [How to setup the build environment](https://github.com/sunduchkov/aloop/wiki/Host-Setup-for-Cross-build).

If you just received the ARM computer you will be interested how to bring it life. Please refer to the [start](https://github.com/sunduchkov/aloop/wiki) page.

After Ubuntu setup please check which audio hardware is available on your system. The easiest way to do this is:

>aplay -l # this is standard tool from alsa-util package

Before run audio program usually [ALSA Mixer Setup](https://github.com/sunduchkov/aloop/wiki/ALSA-Mixer) is required.

>alsamixer # this is standard tool from alsa-util package

It is recommended to add user to audio group otherwise root privileges (or sudo command) will be required.

Start program without parameters will run audio system with default parameters. If something goes wrong please run program with -h option to see what can be changed.
