# Installation Guidelines

1) after Installing Docker Desktop following this **md**  https://github.com/eng-Aly/Os_labs/blob/lab4/Readme.md
2) install the **Docker Image**
```
sudo apt update
sudo apt install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
``` 
4) start the **docker service** 
```   
sudo systemctl start docker
sudo systemctl enable docker
```
5) verify docker version
```   
sudo systemctl status docker
docker --version
``` 
it should show something like this
```
Loaded: loaded (/usr/lib/systemd/system/docker.service; enabled; preset: enabled)
Active: active (running) since Mon 2026-05-04 18:59:01 EEST; 1h 11min ago  
```
6) Pull the pinto image `sajedalmorsy/pintos:1.0` from Docker Hub:
```
sudo docker pull sajedalmorsy/pintos:1.0
```

7) Clone the repo:
```
git clone git@github.com:eng-Aly/dockerized-pintos.git
```
8) From the directory, in which the repo is cloned (when you hit ls you should see the repo ), run a container from the pulled image and attach the repo as a volume:
```
sudo docker run --platform linux/amd64 --rm -it -v $(pwd)/dockerized-pintos:/root/pintos sajedalmorsy/pintos:1.0
```

9) You should now be inside of the running container. Navigate to:
```
cd ~/pintos
```
10) give permisions to git inside the image , note that you may need to set a new ssh key to the image too if you want to push from it (be carefull not to push it with the image i recomend git operations done outside the image anyway)
**do this commands in the hostmachine**
```
sudo chown -R $USER:$USER ~/(REPO_PATH)/dockerized-pintos

```

11)  **return to the docker image** Update the permissions and exclude it from git:
```
git config --global --add safe.directory /root/pintos
chmod -R 777 .
git config core.filemode false
```
12) Build the utils:
```
cd ./src/utils
make clean
make
```
**it may show a warning ignore it** 

13) Check out the branch of the current project phase.\
For Phase 1 (Threads), checkout `threads-dockerized` \
For Phase 1 (Userprog), checkout `user-prog-dockerized`
```
git checkout threads-dockerized
```
14)  Build the current phase code:
```
cd ~/pintos/src/threads/
make clean
make
pintos run alarm-multiple
```
Or, for phase 2
```
cd ~/pintos/src/userprog/
make clean
make
```

15) checkout your current working branch 
    

    

Finally, after making the required code changes, run the following to test you code and calculate your grade from the directory that corresponds to the current phase, `~/pintos/src/threads` or `~/pintos/src/userprog`:
```
make grade
```


<br><br><br><br>
Notes:
- If you see permission issue at any stage, run the following from the `~/pintos` directory inside the container:
```
chmod -R 777 .
```
- For phase 2 (Userprog), you may need to do the build steps of both phases as they do depend on each others in some areas.
