# GTK4-slow-launch-speed-test

Two minimal C applications that use **GTK 3** and **GTK 4** that load and display an image. Used for comparing application **launch speed**.

Each binary prints launch-time milestones to **stderr** so the variants can be measured side by side.

## Build

```sh
./build-and-run.sh
```

# Results

<img width="460" height="269" alt="image" src="https://github.com/user-attachments/assets/cf8c61bc-0e01-47f1-ab9d-1b015c38b97b" />

**GTK4** takes almost 10x to launch on my system

# Why

The reason is that GTK4 takes too much time reading `XDG_DATA_DIRS` on my system, setting `XDG_DATA_DIRS` to a non existing location cuts the time down to 60ms: 

<img width="635" height="145" alt="image" src="https://github.com/user-attachments/assets/3878877c-fb8c-4f9b-a348-85c1ba846089" />

---

This is still a significant degradation, it is still almost 2x the launch time of GTK3.

The reason for this other delay is that GTK4 is hardware accelerated, forcing the software renderer (which is what GTK3 uses) fixes that problem: 

```sh
GDK_RENDERER=cairo GDK_DISABLE=gl,vulkan
```

<img width="823" height="96" alt="image" src="https://github.com/user-attachments/assets/5ddb5295-549e-4e80-9e82-73623263ea5d" />

