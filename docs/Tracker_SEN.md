Tracker src in haiku at
```
haiku/src/kits/tracker
```

after build, generated `libtracker.so` in
```
haiku/generated/objects/haiku/x86_64/release/apps/tracker/Tracker
```

Tracker itself is just a shell application using libtracker and located in
```
haiku/src/apps/tracker/
```
needs also to be rebuilt.

Then, to run the modified (SEN-itized:) Tracker:
* put libtracker and Tracker into a convenient local directory or in SEN tree.
* kill builtin Tracker and prevent it from automatically restarting itself:
``` 
launch_roster stop x-vnd.be-trak
``` 
* start new Tracker in all its semantic glory!
