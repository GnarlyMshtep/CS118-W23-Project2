**Cur, def some bug in pkg drops**

### Tests to run 
- check failing with proxy working `python3 rdproxy.py 5000 9999 0.1`
- compare output to "large file transmission with no packet loss" 
  - https://docs.google.com/document/d/1p1woH3vKU5hAxrpb4j5aT0eRxoYkSO-fyi4b_32bUTE/edit
- file of 10MB
- test without package loss
- with package lost
- somehow check window size
- go through specs and mark everything correct or incorrect
- sequencial client connects, with various file size, etc
  - check the files from each are correct
- set packet loss rate really high (e.g. 95%) 
-   The file transmission should be completed successfully unless the packet loss rate is set to 100%.

### Targets
- no packet loss transporting a big file in packets
  - check output using `test_format.py`


### Questions
- do we need to handle file reading errors? 
- **when does the client send a dup-ack?**
- what if our filesize is a multiple PAYLOAD_SIZE, so that next we try to read from m and read 0?
- how does connection tear down work after we have sent all data? definetly wait to recieve all ACKs, right? 
-     //! what if our filesize is a multiple PAYLOAD_SIZE, so that next we try to read from m and read 0?
- **what if one of the handshake packets is lost?**
  - I think the code given to us already handles this

### Issues
- what if last handshake message drops? 
- some sort of stopping to timeout state?


### Run examples: 
- `./server 5000 25157`
- `./client localhost 9999 2254 hpmor0.tex`
- `python3 rdproxy.py 5000 9999 .1`