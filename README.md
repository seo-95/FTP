# Content:
A simple wrapper for client-server file transfer. <br>
The folder server1 contains an example of usage for a sequential server while the server2 folder contains the realization of a concurrent server based on processes.
<br>
Realization of a concurrent server based on threads is also possible.
<br>
The code contains references to sockwrap.c and errlib.c created by Stevens.
<br>


# Exchanged messages: 
Client file request:
```
|G|E|T| |<filename>|CR|LF|
```
Client quit:
```
|Q|U|I|T|CR|LF|
```
Server error:
```
|-|E|R|R|CR|LF|
```
Server send the following message after the request (if everything is ok):
```
|+|O|K|CR|LF|B1|B2|B3|B4|T1|T2!|T3|T4|<File content>|
```
Where the sequence <B1 B2 B3 B4> is the byte dimension of the file (on 4 bytes) and <T1 T2 T3 T4> is the file timestamp.



# How to use:
Compile
- client: <br>
```
gcc -std=gnu99 -o client client1/*.c *.c -Iclient1 -lpthread -lm
```
- sequential server: <br>
```
gcc -std=gnu99 -o server server1/*.c *.c -Iserver1 -lpthread -lm
```
- concurrent server: <br>
```
gcc -std=gnu99 -o server server2/*.c *.c -Iserver2 -lpthread -lm
```

Run
- client: <br>
```
./client1 <server_addr> <server_port> <file1> <file2> ... <filen>
```
- server: <br>
```
./server1 <server_port>
```
```
./server2 <server_port>
```

