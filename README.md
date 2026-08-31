[![progress-banner](https://backend.codecrafters.io/progress/http-server/c40e280b-14bd-434d-bb41-fb0fb7c494c3)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

This is a starting point for C++ solutions to the
["Build Your Own HTTP server" Challenge](https://app.codecrafters.io/courses/http-server/overview).

[HTTP](https://en.wikipedia.org/wiki/Hypertext_Transfer_Protocol) is the
protocol that powers the web. In this challenge, you'll build a HTTP/1.1 server
that is capable of serving multiple clients.

Along the way you'll learn about TCP servers,
[HTTP request syntax](https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html),
and more.

**Note**: If you're viewing this repo on GitHub, head over to
[codecrafters.io](https://codecrafters.io) to try the challenge.

# Passing the first stage

The entry point for your HTTP server implementation is in `src/server.cpp`.
Study and uncomment the relevant code, and push your changes to pass the first
stage:

```sh
git add .
git commit -m "pass 1st stage" # any msg
git push origin master
```

Time to move on to the next stage!

# Stage 2 & beyond

Note: This section is for stages 2 and beyond.

1. Ensure you have `cmake` installed locally
1. Run `./your_server.sh` to run your program, which is implemented in
   `src/server.cpp`.
1. Commit your changes and run `git push origin master` to submit your solution
   to CodeCrafters. Test output will be streamed to your terminal.



# To run in local host
g++ server.cpp request_handler.cpp -o server -lz

# You’re using WSL (Windows Subsystem for Linux) to run your server, and that changes how networking works, because your server is technically running inside a virtualized Linux environment with its own IP (172.20.176.1).

Find the WSL IP

Inside WSL, run:

`ip addr show eth0`


You’ll see something like:

`inet 172.20.176.123/20`


That’s your WSL instance’s internal IP.

Create a Port Forward from Windows to WSL

Open PowerShell as Administrator and run:

`netsh interface portproxy add v4tov4 listenport=8000 listenaddress=0.0.0.0 connectport=8000 connectaddress=172.20.176.123`


This command forwards all traffic coming to port 8000 on Windows → to your WSL server.

Allow the Port in Windows Firewall 

Also in PowerShell (admin):

`netsh advfirewall firewall add rule name="WSL Port 8000" dir=in action=allow protocol=TCP localport=8000`
