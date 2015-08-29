

gcc acdfuse.c `pkg-config fuse --cflags --libs``pkg-config sqlite3 
--cflags --libs`` pkg-config libcurl --cflags --libs` -ggdb -o acdfuse;
