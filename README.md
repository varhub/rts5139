# A DKMS module for Realtek Semiconductor Corp. RTS5139 / RTS5129 Card Reader Controller

# Description

This source is forked from <a href="https://github.com/varhub/rts5139">varhub</a> thanks to him.

This module need DKMS, build essential and kernel headers to install.

# Installation
1. Clone or download this repo, if you download it, you must extract first.
2. Add it to DKMS as below

   <code>sudo dkms add ./Realtek-RTS5139-RTS5129-DKMS-master/</code>
3. Install it using DKMS as below

   <code>sudo dkms install realtek-rts5139-rts5129-varhub-kr-git/1.04</code>
4. Reboot or restart your GNU/Linux machine to check are your Realtek RTS5139 / RTS5129 Card Reader Controller is functioned.

# Uninstall
1. Uninstall it using DKMS as below

   <code>sudo dkms remove realtek-rts5139-rts5129-varhub-kr-git/1.04 --all</code>
4. Reboot or restart your GNU/Linux machine.


# References 
http://ubuntuone.com/p/153B/

http://www.marqueta.org/lector-de-tarjetas/

https://lkml.org/lkml/2012/3/27/345
