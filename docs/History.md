# First Run

startup `semantic_serve`:
```
~/Develop/SEN/sen-core> dist/semantic_server &
~/Develop/SEN/sen-core> ps
/boot/home/Develop/SEN/sen-core/dist/semantic_server  3604        1    0    0 
```

sending messages to the server via `hey` CLI tool:
```
~/Develop/SEN/sen-core> hey semantic_server 'SCst' 
Reply BMessage('SCrs'):
   "status" (B_STRING_TYPE) : "operational"
   "healthy" (B_BOOL_TYPE) : TRUE 
   "result" (B_INT32_TYPE) : 0 (0x00000000)

~/Develop/SEN/sen-core> hey semantic_server 'SCvs' Reply BMessage('SCri'):
   "info" (B_STRING_TYPE) : "SEN Core v0.0.0-proto1" "result" (B_INT32_TYPE) : 0 (0x00000000)
```
