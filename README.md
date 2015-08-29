##About
This project uses sqlite3 libcurl and uses the acd_cli sqlite db.
This idea is to accelerate the acd_cli fuse mount using some c.

##build
`gcc acdfuse.c `pkg-config fuse --cflags --libs``pkg-config sqlite3 --cflags --libs`` pkg-config libcurl --cflags --libs` -ggdb -o acdfuse;`

##mount
./acdfuse [-d]  mountpoint [-o options]

##Benchmark
1000 folders and 1000 files

`time ls /acd/test/lots' 
c - 0m30.047s
python - 0m35.467s

