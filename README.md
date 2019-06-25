# SuperChat
A Chatting application made using C++ and ncurses on the command line. This application uses the asio library to implement multiple chatroom application. 

# How to Run
The following commands should be run in the terminal before the program is run.
     
    sudo apt-get install libboost-all-dev
    sudo apt-get install libncurses5-dev
     
After Installing the above packages type the command below to make the executables.

    make
    
The client and server can be run on different terminals or computers using the following commands

    ./chat_server <port_number>
    ./chat_client <IP_Address > <port_number>
  
# Note
The port number should be the same for the clients and the server to send and recieve messages between different clients.

# Commands Supported
The follwing commands are supported in the application:

1.   /help

          This Command lists all the commands supported on the terminal.
2.   /quit
          
          Quits the program.
3.   /change chatroom
          
          This command lists all the available chatrooms and allows the user to create chatrooms and change between them.
4.   /delete chatroom

          This command lets the user delete a chatroom. A chatroom cannot be deleted if there is a client inside it. The Main lobby cannot be deleted.
5.   /ban

          Bans the client specified. All messages from the banned users are ignored.
6.   /unban

          Allows the user to unban the banned users.
