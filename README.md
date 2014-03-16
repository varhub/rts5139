Realtek Semiconductor Corp. RTS5139 Card Reader Controller | Fedora 18 manual install
=======

I have a Lenovo Ideapad z580 and recently discover that sdcard reader not works.
After some investigation, and driver compilation, it works and I'm happy ;)

Tested in my Lenovo Ideapad z580

Original source:
http://ubuntuone.com/p/153B/

Required patch:
http://www.marqueta.org/lector-de-tarjetas/

Additional patch:
https://lkml.org/lkml/2012/3/27/345


Using this source to fix
===

1. Download code
2. make
3. sudo make install
4. sudo depmod -a
5. sudo modprobe rts5139
