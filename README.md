bcc_server
==========

Purpose / Goal of this project
--------------
This project was started as a project for the u23 project of the Chaos Computer Club Cologne (C4).
<a href="https://github.com/RobertSchneider">RobertSchneider</a>, <a href="https://github.com/Trip-eng">Trip-eng</a> and I were tasked to create a web based chat system for use in <a href="http://freifunk.net/en/">Freifunk</a>.
The BroadChaos Chat is the result of our work.

The bcc_server handles incomming WebSocket - connections.<br>
After the handshake it will send every incomming chat-message to the given broadcast address.<br>
For example:

```
./bcc_server 192.168.0.255
```

This is still in development and there is probably some inefficient code somewhere... .<br>
Todo:
--------------
- Better error handling
- Config file? (or parameters like 'MAX_CLIENTS' etc.)
- Cross-compile for OpenWrt
- Method descriptions
- Testing

Dependencies
--------------
- <a href="https://github.com/OSpringer/bcc-library">Bcc-Library</a>
- <a href="https://github.com/json-c/json-c">json-c</a>

