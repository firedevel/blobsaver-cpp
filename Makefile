
CC = g++

blobsaver:
	$(CC) -o blobsaver bs.cpp -I/usr/include/libxml2 -lxml2 -lcurl
