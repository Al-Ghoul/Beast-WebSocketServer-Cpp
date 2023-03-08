# Beast-WebSocketServer-Cpp

As The name says this is a WebSocket Server written in C++ using [*Boost::Beast*](https://www.boost.org/doc/libs/1_81_0/libs/beast/doc/html/index.html) framework.

## User Authentication
Once a user establishes a connection, the server expects a JWT that represents the user upon connection
if the server couldn't read, verify and validate a JWT that is issued by the same WebServer referring to "[*GameServer-Mockup-Web*](https://github.com/Al-Ghoul/GameServer-Mockup-Web)" here (Validating with the same SECRET key for sure). The Connection is *rejected*.

After a **Successful Connection** the server notifies All currently connected users with the new user's presence/connection, Sends to the new user the previously/currently connected users. After that the Server acts like a *State Machine* that handles only a USER_MESSAGE request.

If you're going to use this for chat messages OR re-write your own, I've to say that the complexity is not worth it, you can use something like [Socket.io](https://socket.io/)
or any other alternatives, However if you are to deploy an online game that's playable on browsers/mobile then performance is mandatory, a game like [SlitherIO](https://slither.io/) had it's server written using beast.


### Technologies used in this project

- [Boost::Beast](https://www.boost.org/doc/libs/1_81_0/libs/beast/doc/html/index.html)
- [Boost::JSON](https://www.boost.org/doc/libs/1_81_0/libs/json/doc/html/index.html)
- [JWT-Cpp](https://github.com/Thalhammer/jwt-cpp)
  - [OpenSSL](https://github.com/openssl/openssl)
- [PosgtresSQL libpqxx](https://github.com/jtv/libpqxx)
