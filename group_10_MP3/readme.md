1. make
2. sudo insmod mp3.ko
3. cat /proc/devices
   and check Major device number for MP3_CDEV
4. cd /dev
5. sudo mknod node c "majordevnum" 0
6. run tests (make t1 or make t2)
7. when tests end, cp monitor /dev
8. sudo ./monitor > "destination_file"

