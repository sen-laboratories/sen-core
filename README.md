# sen-core

## SEN Server

Haiku server process acting as the foundation of the semantic layer

## Build

```
> make
```

## Run

```
> dist/semantic_server &
```

## Demo Proto2

seg up index and attributes (until add relations is implemented):

```bash
> ID=$(stat -c "%i" README.md)
> mkindex -t string "SEN:_id"
> mkindex -t string "SEN:referencedBy"
> addattr -t string "SEN:_id" $ID README.md
> addattr -t string "SEN:referencedBy" $ID Makefile
```

send message and get relation targets result:

```bash
> hey semantic_server 'SCrt' with source=Makefile and relation=SEN:referencedBy
resolving targets for id 3210985
Results of query "SEN:_id == 3210985":
        /boot/home/Develop/SEN/sen-core/README.md
Reply BMessage('SCre'):
   "statusMessage" (B_STRING_TYPE) : "get targets for relation SEN:referencedBy from Makefile"
   "result" (B_INT32_TYPE) : 0 (0x00000000)
```
