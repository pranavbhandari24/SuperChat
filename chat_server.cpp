//
// chat_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2018 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include "asio.hpp"
#include "chat_message.hpp"
#include <vector>
#include <fstream>

std::string client_name;
using asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<chat_message> chat_message_queue;

//----------------------------------------------------------------------

chat_message string_to_msg(std::string n)
{
    //Function to convert a string to a chat message.
    char name[n.length()+ 1];
    strcpy(name, n.c_str());
    chat_message msg1;
    msg1.body_length(std::strlen(name));
    std::memcpy(msg1.body(), name, msg1.body_length());
    msg1.encode_header();
    return msg1;
}



class chat_participant
{
  private:
    std::string nickname;
  public:
    virtual ~chat_participant() {}
    virtual void deliver(const chat_message& msg) = 0;
    void set_nickname(std::string n)
    {
      nickname = n;
    }
    std::string get_nickname()
    {
      return nickname;
    }
};

std::vector<std::string> names;

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

void on_quit(std::string name)
{
  for(int i=0;i< (int)names.size();i++)
  {
    if(names[i] == name)
    {
      std::cout<<"Erased "<<names[i]<<" from the list of Nicknames.\n";
      names.erase(names.begin()+i);
    }
  }
}

class chat_room
{
public:
  int num_of_participants()
  {
    return participants_.size();
  }
  void clear_messages()
  {
    //clearing the list of recent messages.
    recent_msgs_.clear();
  }
  void join(chat_participant_ptr participant)
  {
    participants_.insert(participant);
    for (auto msg: recent_msgs_)
      participant->deliver(msg);
  }
  void join_message(std::string str)
  {
    str = str + " has joined the chat.";
    chat_message msg1 = string_to_msg(str);
    deliver(msg1);
  }

  void exit_message(std::string str)
  {
    str = str + " has left the chat.";
    chat_message msg1 = string_to_msg(str);
    deliver(msg1);
  }

  void leave(chat_participant_ptr participant)
  {
    participants_.erase(participant);
    exit_message(participant->get_nickname());
  }

  void set_chatname(std::string str)
  {
    chat_room_name = str;
  }

  std::string get_chatname()
  {
    return chat_room_name;
  }

  void deliver(const chat_message& msg)
  {
    recent_msgs_.push_back(msg);
    while (recent_msgs_.size() > max_recent_msgs)
      recent_msgs_.pop_front();

    for (auto participant: participants_)
      participant->deliver(msg);
  }

private:
  std::set<chat_participant_ptr> participants_;
  enum { max_recent_msgs = 100 };
  chat_message_queue recent_msgs_;
  std::string chat_room_name;
};

//----------------------------------------------------------------------

class chat_session
  : public chat_participant,
    public std::enable_shared_from_this<chat_session>
{
public:
  chat_session(tcp::socket socket, chat_room room[10])
    : socket_(std::move(socket)),
      room_(room)
  {
    chat_room_number = 0;
  }

  void start()
  {
    do_read_header(1);
  }

  void deliver(const chat_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      do_write();
    }
  }

private:
  void do_read_header(int t)
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(read_msg_.data(), chat_message::header_length),
        [this, self,t](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec && read_msg_.decode_header())
          {
            do_read_body(t);
          }
          else
          {
            //sending a message to all the clients in the chatroom that the user has left.
            on_quit(shared_from_this()->get_nickname());
            room_[chat_room_number].leave(shared_from_this());
          }
        });
  }

  void do_read_body(int t)
  {
    auto self(shared_from_this());
    asio::async_read(socket_,
        asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this, self,t](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            std::string temp = read_msg_.body();
            int len = read_msg_.body_length();
            if(t == 1) //First time the user entered the port. The code checks if the nickname entered already exists.
            {
              client_name = temp.substr(0,len);
              if(std::find(names.begin(),names.end(),client_name)!= names.end())
                shared_from_this()->deliver(string_to_msg("~!Name"));
              else
              {
                room_[chat_room_number].join(shared_from_this());
                shared_from_this()->deliver(string_to_msg("~Name")); //sending a message back to the client.
                shared_from_this()->set_nickname(client_name);
                names.push_back(client_name);
                //room_[chat_room_number].join_message(shared_from_this()->get_nickname());
              }
            }
            else if(temp[0] == '~')
            {
              client_name = temp.substr(1,len-1);
              if(std::find(names.begin(),names.end(),client_name)!= names.end())
                shared_from_this()->deliver(string_to_msg("~!Name"));
              else
              {
                room_[chat_room_number].join(shared_from_this());
                shared_from_this()->deliver(string_to_msg("~Name"));
                shared_from_this()->set_nickname(client_name);
                names.push_back(client_name);
                //room_[chat_room_number].join_message(shared_from_this()->get_nickname());
              }
            }
            else if(temp[0]=='\\') //Changing chatroom for a particular user.
            {
              chat_message msg;
              std::cout<<"changing chatroom for "<<shared_from_this()->get_nickname()<<" to "<<temp.substr(1,1)<<"\n";
              room_[chat_room_number].leave(shared_from_this());
              chat_room_number = temp[1] -'0';
              if(room_[chat_room_number].get_chatname() == "NULL") //The chatroom specified does not exist.
              {
                msg = string_to_msg("\\\\"); //Sending a message to the user that the chatroom doesnt exist.
                room_[chat_room_number].join(shared_from_this());
              }  
              else //Changing chatroom if the chatroom specified exists.
              {
                std::string current = room_[chat_room_number].get_chatname();
                msg = string_to_msg("\\"+current);
                room_[chat_room_number].join(shared_from_this());
                //room_[chat_room_number].join_message(shared_from_this()->get_nickname());
              }  
              shared_from_this()->deliver(msg);
            }
            else if(temp[0]=='!') //Changing name of the chatroom.
            {
              std::cout<<"Changed name of chatroom "<<chat_room_number <<" to "<<temp.substr(1,len-1)<<".\n";
              room_[chat_room_number].set_chatname(temp.substr(1,len-1));
              //room_[chat_room_number].join_message(shared_from_this()->get_nickname());
            }
            else if(temp[0] == '*') //Deleting the specified chatroom.
            {
              int num = temp[1] - '0';
              if(room_[num].get_chatname() == "NULL") // The chatroom doesnt exist.
                shared_from_this()->deliver(string_to_msg("*!"));
              else if(room_[num].num_of_participants() != 0)
                shared_from_this()->deliver(string_to_msg("*!"));
              else 
              {
                std::cout<<"Deleted Chatroom number "<<num<<".\n";
                room_[num].set_chatname("NULL");
                room_[num].clear_messages();
                shared_from_this()->deliver(string_to_msg("**"));
              }
            }
            else if(temp[0] =='L' && temp[1] =='O' && temp[2] == 'R') //Returning a List of all chatrooms to the user.
            {
              std::string result = "[]LOR:Number    Name of Chatroom";
              for(int i=0;i<10;i++)
              {
                if(room_[i].get_chatname() != "NULL")
                  result = result + "\n\t" + "     "+ std::to_string(i) + "      " +room_[i].get_chatname();
              }
              result = result;
              shared_from_this()->deliver(string_to_msg(result));
            }
            else //Just a normal message.
            {
              add_common_reply(temp.substr(2,len-2));
              room_[chat_room_number].deliver(string_to_msg(temp.substr(0,len)));
            }
            do_read_header(0);
          }
          else
          {
            //sending a message to all the clients in the chatroom that the user has left.
            on_quit(shared_from_this()->get_nickname());
            room_[chat_room_number].leave(shared_from_this());
          }
        });
  }

  void do_write()
  {
    auto self(shared_from_this());
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this, self](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            write_msgs_.pop_front();
            if (!write_msgs_.empty())
            {
              do_write();
            }
          }
          else
          {
            //sending a message to all the clients in the chatroom that the user has left.
            on_quit(shared_from_this()->get_nickname());
            room_[chat_room_number].leave(shared_from_this());
          }
        });
  }

  void add_common_reply(std::string message)
  {
    //Separate main part of message from beginning
    std::string delim = ": ";
    int message_start = message.find(delim) + 2; //Add 2 because it finds location of : 
    std::string reply = message.substr(message_start);
    std::ifstream ifile;
    std::ofstream ofile;

    ifile.open("~.SuperChat.txt");
    if(ifile.is_open() )   //Checking for existing file
    {
      ofile.open("~.SuperChat_new.txt", std::ofstream::app);
      std::string line;
      std::string delim1 = " ";
      bool found = false;
      while(std::getline(ifile, line) )   //Check if reply exists in file already
      {
        std::string line_mod = line.substr(0, line.rfind(delim1) );   //Gets rid of number at end of string
        if(line_mod == reply)
        {
          found = true;
          int reply_count = std::stoi(line.substr(line.rfind(delim1) + 1) ) + 1;    //Converts number at end of line into an int
          reply = reply + " " + std::to_string(reply_count);
          ofile << reply << std::endl;
        }
        else
        {
          ofile << line << std::endl;
        }
        
      }
      ifile.close();
      ofile.close();
      std::remove("~.SuperChat.txt");
      std::rename("~.SuperChat_new.txt", "~.SuperChat.txt");

      if(!found)    //Reply not found
      {
        reply = reply + " 1";   //Used to keep track of number of appearances
        ofile.open("~.SuperChat.txt", std::ofstream::app);
        ofile << reply << std::endl;
        ofile.close();
      }
    }
    else    //File doesn't exist yet
    {
      reply = reply + " 1";   //Used to keep track of number of appearances
      ofile.open("~.SuperChat.txt", std::fstream::app);
      ofile << reply << std::endl;
      ofile.close();
    }
  }
  
  tcp::socket socket_;
  chat_room *room_;
  chat_message read_msg_;
  chat_message_queue write_msgs_;
  int chat_room_number;
};

//----------------------------------------------------------------------

class chat_server
{
public:
  chat_server(asio::io_context& io_context,
      const tcp::endpoint& endpoint)
    : acceptor_(io_context, endpoint)
  {
    room_[0].set_chatname("MAIN LOBBY");
    for(int i=1;i<11;i++)
      room_[i].set_chatname("NULL");
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](std::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            std::make_shared<chat_session>(std::move(socket), room_)->start();
          }

          do_accept();
        });
  }

  tcp::acceptor acceptor_;
  chat_room room_[10];
};

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: chat_server <port> [<port> ...]\n";
      return 1;
    }

    asio::io_context io_context;

    std::list<chat_server> servers;
    for (int i = 1; i < argc; ++i)
    {
      tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
      servers.emplace_back(io_context, endpoint);
    }

    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
