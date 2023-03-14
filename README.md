# CS118 Project 2

Repo for Winter 2023, CS118, project 2: Reliable Transfer, by Lam Hoang (805619479, lamhoang1213@g.ucla.edu) and Matan Shtepel (505581312, matan.shtepel@ucla.edu). 

## High Level Design 

This project consists of 2 source files, `client.c` and `server.c` where `client.c` tried to transfer a file to `server.c` over a channel which may drop data. The data is sent via `WND_SIZE`-many pipelined packets, and when loss is detected via a timeout (as described in the Go-Back-N pattern), all the unacknoledged packtets are sent. 

The main-body of `client.c` runs in a while loop running under the condition `!(sent_entire_file && zero_packets_in_transmission)`, where `sent_entire_file` and `zero_packets_in_transmission` are semanticly described boolean indicators. Keeping track of these variables made out code significantly cleaner. Our loop tried to read from the channel, and then branches into 3 (generally not mutually exclusive -- this was important to avoid flooding, particularly on the server) clauses. The first clause checks if an ACK for packets in the current window was recieved and advances the window appropriately. The second clause checks if a timeout has occured, and if so, sends all the unacknowledged message in the current window. The third clause checks if have `WND_SIZE` packets in transport, and if we have less and there is more data to send, it sends another packet.

The main-body of `server.c` runs in a internally-break'ed while loop. It is significantly simpler then the `client.c` while loop, since it only reacts to the clients packets, and doesn't generate its own trafic. The while loop is also made of 3 clauses, one for checking if the client sends a `FIN` request, one to recieve the next expected byte from the client and send an ACK, and one to send a `DUP-ACK` to the client.  

## Issues we ran into 

We ran into 3 main issues: testing networking code is hard, on extremely high drop rates (e.g. `.9`) is difficult to understand if one's code is broken, and our print format was slightly wrong. 

There is no magic solution we know for debugging networking code, and our solution was standard: log and track back to a minimum working example. We built our code carefully and with many comments, testing at every possible incremement. When we had an issue, we tracked back to the minimal working example, read through the transcript (which massively useful) to detect the first discrepency from the expected output. When we detected it, the error was often pretty clear, and we could adjust our code accordingly after a few failed attempts. This sort of debugging was eased by first writing psuedocode, and only then writing the code 

When we tried to run drop rate of `.9`, we couldn't get even medium files to go through the network and din't know if that was due to a bug in our code or because the extremely poor channel. We debugged that for a while. The solution turned out to be giving, submitting to gradescope, and learning that drop rates above `.5` are not tested. Thus, we didn't really need to solve this problem. 

The last problem was also quite solvable, namely, we compared our print format with the expected in gradescope, and learned where we misinterperted the protocol guidelines. For instance, by checking the gradescope output we learned that the server is supposed to send DUP-ACKs when it is sending a duplicate acknowledgement. Fixing this was easy :)

## Additional Resources

We did not use any additional resources. We only added the `<stdbool.h>` library becuase, well, bools are nice :P

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `zip` target to create the submission file as well.

You will need to modify the `Makefile` USERID to add your userid for the `.zip` turn-in at the top of the file.

## Run examples: 

On 3 different shell environments, run: 

* `./server 5000 25157`
* `./client localhost 9999 2254 hpmor0.tex`
* `python3 rdproxy.py 5000 9999 .1`