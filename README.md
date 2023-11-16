# SEN Core - Semantic ExteNsions

## SEN Server

Haiku server process acting as the foundation of the semantic layer

## Build

```
> make
```

## Run

```
> dist/sen_server &
```

## Demo Proto1

set up index and attributes (manually until script/installer and graphical support for adding relations is implemented):

```bash
> ID=$(stat -c "%i" README.md)
> mkindex -t string "SEN:_id"
> mkindex -t string "SEN:referencedBy"
> addattr -t string "SEN:_id" $ID README.md
> addattr -t string "SEN:referencedBy" $ID Makefile
```

send message and get relation targets result (with local example IDs):

```bash
> hey sen_server 'SCrt' with source=Makefile and relation=SEN:referencedBy
resolving targets for file IDs 3210985,3670583...
Results of query "SEN:_id == 3210985":
        /boot/home/Develop/SEN/sen-core/README.md
Results of query "SEN:_id == 3670583":
        /boot/home/Develop/SEN/sen-core/src
Adding to result message:
        /boot/home/Develop/SEN/sen-core/README.md
        /boot/home/Develop/SEN/sen-core/src
Reply BMessage('SCre'):
   "targets" (B_STRING_TYPE) : "/boot/home/Develop/SEN/sen-core/README.md"
   "targets" (B_STRING_TYPE) : "/boot/home/Develop/SEN/sen-core/src"
   "statusMessage" (B_STRING_TYPE) : "found 2 targets for relation SEN:referencedBy from Makefile"
   "result" (B_INT32_TYPE) : 0 (0x00000000)
```
