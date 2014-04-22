Audio path-through program designed to use on embedded ARM computers like Beagleboard xM, Beaglebone Black, Pandaboard operating on Ubuntu. The program uses ALSA interface to communicate with a hardware.

Before start the program it is recommended to add user to audio group otherwise root privileges (or sudo command) will be required.

Please check which audio hardwares are available on your system. The easiest way to do this is:

>aplay -l # this is standard tool from alsa-util package

Usually mixer setup is required.

>alsamixer # this is standard tool from alsa-util package

If your board does not have own audio and you connect aduio through USB you probably need to run mixer with a card number parameter.

>alsamixer -c 1

Start program without parameters will run audio system with default parameters. If something goes wrong please run program with -h option to see what can be changed.

