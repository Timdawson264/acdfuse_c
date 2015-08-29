##About
This project uses sqlite3 libcurl and uses the acd_cli sqlite db.
This idea is to accelerate the acd_cli fuse mount using some c.

##TODO
- seems to have issues on armhf.
- also has issues with relative paths (might be libfuse)

##build
`gcc acdfuse.c \`pkg-config fuse --cflags --libs\`\`pkg-config sqlite3 --cflags --libs\`\`pkg-config libcurl --cflags --libs\` -ggdb -o acdfuse;`

##mount
./acdfuse [-d]  mountpoint [-o options]

##Benchmark
1000 folders and 1000 files

### acdfuse_c
`./acdfuse acd/`
`time find acd/ > /dev/null`

real	0m10.172s
user	0m0.060s
sys	0m0.193s

###acd_cli
`acd_cli mount acd/`
`time find acd/ > /dev/null`

real	4m25.864s
user	0m0.233s
sys	0m0.703s

