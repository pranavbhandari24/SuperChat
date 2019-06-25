//
// chat_client.cpp
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
#include <thread>
#include "asio.hpp"
#include "chat_message.hpp"
#include <ncurses.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <time.h>

using asio::ip::tcp;
int height,width;

typedef std::deque<chat_message> chat_message_queue;
std::string chat_room_name = "\0";
std::string current_chatroom_name = "MAIN LOBBY";
int room_exists = 0;
int name_present = 0;
int delete_chat = 0;
std::string list_of_chatrooms = "\0";
std::string new_name = "\0";

std::vector<std::string> store_messages;
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

//-----------------------NCURSES-----------------------------------------

std::string BackWindow(std::string heading, std::string message, int temp)
{
    WINDOW *back_win =newwin(height-3,width-10,2,5);
    refresh();
    char ca='=';
    box(back_win,0,0); // to customise my border I used wborder 
    int left,right,top,bottom,tlc,trc,blc,brc; 
    char Fake_name[100];
    left=right=(int)'#';
    top=bottom=(int) ca;
    tlc=trc=blc=brc=(int) ca; 

    wattron(back_win,A_BOLD); // to make the string bold
    mvwprintw(back_win,height/8,(width-10)/2-(heading.length()/2),heading.c_str());// to print inside the box
    wattroff(back_win,A_BOLD); 
    if(temp == 0)
    {
      mvwprintw(back_win,height/8+3,(width-10)/2-(message.length()/2),message.c_str());
    }
    else
    {  
      wprintw(back_win," \n\n\t");
      wprintw(back_win," %s",message.c_str());
    }
    wborder(back_win,left,right,top,bottom,tlc,trc,blc,brc);
    wrefresh(back_win); 
    WINDOW *InputWin=newwin(3,32,(height-5),((width)/2-16)); 
	  refresh();
	  box(InputWin,0,0);
	  mvwprintw(InputWin,1,1,""); 
	  wgetstr(InputWin,Fake_name); 
	  wrefresh(InputWin);
    std::string result(Fake_name);
    delwin(InputWin);
    delwin(back_win);
    clear();
    refresh();
    return result;
}

//-----------------------------------------------------------------------

class chat_client
{
public:
  chat_client(asio::io_context& io_context,
      const tcp::resolver::results_type& endpoints)
    : io_context_(io_context),
      socket_(io_context)
  {
    text_box = NULL;
    chat_screen = NULL;
    do_connect(endpoints);
  }

  void build_chat_screen()
  {
    chat_screen = newwin(height-5,width-5,0,0);
    scrollok(chat_screen,TRUE);
    wprintw(chat_screen, "\n");
    wprintw(chat_screen, "|%s\n", current_chatroom_name.c_str());
    wprintw(chat_screen, "|%s\n", "type /help for more info.");
    box(chat_screen,0,0);
    wrefresh(chat_screen);
  }

  void build_text_box()
  {
    text_box = newwin(5, width-5, height-5,0);
    scrollok(text_box,TRUE);
    box(text_box,0,0);
    wrefresh(text_box);
  }

  void delete_text_box()
  {
    delwin(text_box);
    text_box = NULL;
  }

  void delete_chat_screen()
  {
    delwin(chat_screen);
    chat_screen = NULL;
  }

  WINDOW *get_text_box()
  {
    return text_box;
  }

  void display_msg(std::string str)
  {
    if(chat_screen == NULL)
      return;
    wprintw(chat_screen, " %s\n", str.c_str());
    refresh_all();
  }

  char *get_text(WINDOW *win, int y, int x)
  {
    char *input = (char *)malloc(200 * sizeof(char));
    memset(input, '\0', 200 * sizeof(char));
    int i = 0;
    noecho();
    keypad(win, TRUE);
    cbreak();
    wmove(win, y, x + 1);
    int ch = wgetch(win);

    while (ch != '\n')
    {
      if (i == 0 && ch == KEY_BACKSPACE)
      {
      }
      else if (i > 0 && ch == KEY_BACKSPACE)
      {
        mvwdelch(win, y, i + x);
        mvwdelch(win, y, 198);
        wmove(win, y, i + x);
        box(win, 0, 0);
        wrefresh(win);
        i--;
        input[i] = '\0';
      }
      else
      {
        wprintw(win, "%c", (char)ch);
        wrefresh(win);
        input[i] = (char)ch;
        i++;
      }
      ch = wgetch(win);
    }
    cbreak();
    echo();
    wrefresh(win);
    return input;
  }

  void refresh_all()
  {
    box(chat_screen, 0, 0);
    box(text_box, 0, 0);
    wrefresh(chat_screen);
    wrefresh(text_box);
  }

  void send_recent_messages()
  {
    if(chat_screen == NULL)
      return;
    for(auto i =0;i<(int)store_messages.size();i++)
    {
      wprintw(chat_screen, " %s\n", store_messages[i].c_str());
    }
    store_messages.clear();
    refresh_all();
  }

  void write(const chat_message& msg)
  {
    asio::post(io_context_,
        [this, msg]()
        {
          std::string a = msg.body();
          bool write_in_progress = !write_msgs_.empty();
          write_msgs_.push_back(msg);
          if (!write_in_progress)
          {
            do_write();
          }
        });
  }

  void close()
  {
    asio::post(io_context_, [this]() { socket_.close(); });
  }

  void ban_user(std::string user, std::string user_to_ban)
  {
    std::ifstream ifile;
    std::ofstream ofile;

    std::string file_name = user + "_ban_list.txt";

    ifile.open(file_name);
    if(ifile.is_open() )    //Checking for existing file
    {
      std::string line;
      bool found = false;

      while(std::getline(ifile, line) )   //Check if user exists in ban list already
      {
        if(line == user_to_ban)
        {
          found = true;
          display_msg("User "+ user_to_ban +" Already Banned.");
        }
      }
      ifile.close();

      if(!found)    //User not found in ban list
      {
        ofile.open(file_name, std::ofstream::app);
        ofile << user_to_ban << std::endl;
        ofile.close();
        display_msg("Banned "+user_to_ban+".");
      }
    }
    else    //File doesn't exist yet
    {
      ofile.open(file_name, std::fstream::app);
      ofile << user_to_ban << std::endl;
      ofile.close();
      display_msg("Banned "+user_to_ban+".");
    }
  }

  void unban_user(std::string user, std::string user_to_unban) //methord called to unban the user
  {
    std::ifstream ifile;
    std::ofstream ofile;

    std::string file_name = user + "_ban_list.txt";
    std::string file_name_new = user + "_ban_list_new.txt";

    char c_file_name[file_name.length() + 1];
    char c_file_name_new[file_name_new.length() + 1];

    strcpy(c_file_name, file_name.c_str() );    //converting std::string into char[]
    strcpy(c_file_name_new, file_name_new.c_str() );

    ifile.open(c_file_name);
    if(ifile.is_open() )    //Checking for existing file
    {
      ofile.open(c_file_name_new, std::ofstream::app);
      std::string line;
      bool found = false;

      while(std::getline(ifile, line) )   //Check if user exists in ban list
      {
        if(line == user_to_unban)
        {
          found = true;
          display_msg("Unbanned " + user_to_unban);
        }
        else
        {
          ofile << line << std::endl;
        }
      }
      ifile.close();
      ofile.close();

      std::remove(c_file_name);
      std::rename(c_file_name_new, c_file_name);

      if(!found)
      {
        display_msg("User " + user_to_unban + " not found on the ban list.");
      }
    }
  }

  bool check_ban(std::string user, std::string user_to_check)
  {
    std::ifstream ifile;

    std::string file_name = user + "_ban_list.txt";

    ifile.open(file_name);
    if(ifile.is_open() )    //Checking for existing file
    {
      std::string line;

      while(std::getline(ifile, line) )   //Check if user exists in ban list already
      {
        if(line == user_to_check)
        {
          return true;
        }
      }
      ifile.close();
    }

    return false;
  }

  void set_nickname(std::string n_name)
  {
    nickname = n_name;
  }

  
private:
  void do_connect(const tcp::resolver::results_type& endpoints)
  {
    asio::async_connect(socket_, endpoints,
        [this](std::error_code ec, tcp::endpoint)
        {
          if (!ec)
          {
            do_read_header();
          }
        });
  }

  void do_read_header()
  {
    asio::async_read(socket_,
        asio::buffer(read_msg_.data(), chat_message::header_length),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          if (!ec && read_msg_.decode_header())
          {
            do_read_body();
          }
          else
          {
            socket_.close();
          }
        });
  }

  void do_read_body()
  {
    asio::async_read(socket_,
        asio::buffer(read_msg_.body(), read_msg_.body_length()),
        [this](std::error_code ec, std::size_t /*length*/)
        {
          //Comes here when a message is recieved from the server.
          std::string temp = read_msg_.body();
          temp = temp.substr(0,read_msg_.body_length());
          if (!ec)
          {
            //Various Checks to see if the message recieved are special messages
            if(temp[0] == '\\') //Message recieved about changing chatrooms
            {
              if(temp[1]=='\\') //The chatroom the user wants to join doesnt exist
                chat_room_name = "!!";
              else  //The chatroom the user wants to join exists
                current_chatroom_name = temp.substr(1,temp.length());
              room_exists =1;
              do_read_header();
            }
            else if(temp[0] == '~') //Message recieved about Nickname
            {
              if(temp[1] == '!')
                new_name = "!!";
              name_present = 1;
              do_read_header();
            }
            else if(temp[0] == '*') //Message recieved about deleting chatrooms
            {
              if(temp[1]=='!')
                delete_chat = 1;
              else
                delete_chat = 2;
              do_read_header();
            }
            else if(temp[0] == '[' && temp[1] == ']' && temp[2] == 'L' && temp[3] == 'O' && temp[4] =='R') //Checking if the message recieved is the list of chatrooms
            {
              temp= temp.substr(6,temp.length());
              list_of_chatrooms = temp;
              do_read_header();
            }
            else //Just a regular message
            {
              std::string delim = " [";
              int nickname_loc_end = temp.find(delim);
              std::string user_to_check = temp.substr(0, nickname_loc_end);
              //Checking if the sender of the message is banned by the client
              //If the sender is banned, the message is not displayed
              if(!check_ban(nickname, user_to_check))
              {
                //Checking if the screen to print exists
                if(chat_screen == NULL)
                {
                  //The screen doesnt exist, therefore all the messages are saved in the vector till the chat screen is created
                  store_messages.push_back(temp);
                }
                else
                {
                  //Displaying the message recieved to the client
                  char input[500] = {'\0'};
                  std::strncpy(input, read_msg_.body(), read_msg_.body_length());
                  wprintw(chat_screen, " %s\n", input);
                  box(chat_screen, 0, 0);
                  refresh_all();
                }
              }
              do_read_header();
            }
          }
          else
          {
            socket_.close();
          }
        });
  }

  void do_write()
  {
    asio::async_write(socket_,
        asio::buffer(write_msgs_.front().data(),
          write_msgs_.front().length()),
        [this](std::error_code ec, std::size_t /*length*/)
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
            socket_.close();
          }
        });
  }

private:
  asio::io_context& io_context_;
  tcp::socket socket_;
  chat_message read_msg_;
  chat_message_queue write_msgs_;
  WINDOW *chat_screen;
  WINDOW *text_box;
  std::string nickname;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: chat_client <host> <port>\n";
      return 1;
    }
    //Initializing Ncurses
	  initscr();
    getmaxyx(stdscr, height, width);
    cbreak();
    echo();
    std::string n_name;
    //Getting name from the login screen
    n_name = BackWindow("WELCOME TO SUPERCHAT","Enter Nickname",0);
    asio::io_context io_context;

    tcp::resolver resolver(io_context);
    auto endpoints = resolver.resolve(argv[1], argv[2]);
    chat_client c(io_context, endpoints);
    
    time_t my_time;
    int current_chatroom = 0;
    int temp;
    std::thread t([&io_context](){ io_context.run(); });
    char line[chat_message::max_body_length + 1];
    c.write(string_to_msg(n_name));
    //Checking for Same nicknames
    while(true)
    {
      //Infinite loop till the server responds.
      while(name_present == 0) {}
      if(new_name != "\0")  //Checking if the name already exists.
      {
        //Prompting the user to enter the name again.
        std::string a = BackWindow("ERROR : Name already Exists","Enter another Nickname",0);
        new_name = "\0";
        name_present = 0;
        n_name = a;
		    a="~"+a; 
        clear();
        //Sending message to the server with the new name 
        c.write(string_to_msg(a));
      }
      //The process of checking the name continues till the user enters a valid name
      else //The name is valid
        break;
    }
    //End of same nickname check
    c.set_nickname(n_name);
    //Building chating screen
    clear(); 
    cbreak();
    //Building the Chatting screen
    c.build_chat_screen();
    c.build_text_box();
    c.refresh_all();
    c.send_recent_messages();
    while (true)
    {
      //Rebuilding the chat and text box everytime a message is sent to avoid bugs.
      c.delete_text_box();
      c.build_text_box();
      std::string data = c.get_text(c.get_text_box(),1,0); //calling function to get data from the text_box (NCURSES)
      data.copy(line, data.size() + 1); 
      line[data.size()] = '\0'; //data is transferred to line
      chat_message msg;
      //Checking if the user wants to change chatrooms
      if(strcmp(line,"/change chatroom") == 0)
      {
        //Sending message to the server to provide with the list of chatrooms
        c.write(string_to_msg("LOR"));
        //Infinite loop till the server responds (waiting for the server to respond)
        while(list_of_chatrooms == "\0") {}
        clear();
        refresh();
        //Prompting the user to enter the number of the chatroom they want to join
        std::string number = BackWindow("Enter Chatroom Number",list_of_chatrooms+"\n\tFor any other number chatroom will be created.\0",1);
        list_of_chatrooms = "\0";
        //Changing chatrooms if the number entered is valid.
        try
        {
          temp = std::stoi(number);
        }
        catch(const std::exception& e)
        {
          c.refresh_all();
          c.send_recent_messages();
          c.display_msg("Invalid Number.");
          continue;
        }
      
        if(current_chatroom == temp)
        {
          c.refresh_all();
          c.send_recent_messages();
          c.display_msg("In the chatroom already.");
          continue;
        }
        if(temp >9 || temp<0)
        {
          c.refresh_all();
          c.send_recent_messages();
          c.display_msg("Maximum chatroom limit is 9. Enter a chatroom number below 9.");
          continue;
        }
        c.delete_chat_screen();
        c.delete_text_box();
        //Sending a message to the server to check if the chatroom the user wants to join already exists
        std::string chat_room_msg = "\\" +std::to_string(temp);
        msg = string_to_msg(chat_room_msg);
        c.write(msg);
        while(room_exists == 0) {}
        /*
          If the chatroom doesn't exist then the user is prompted to enter the name of the chatroom,
          otherwise if the chatroom already exists then the user just joins the chatroom.
        */
        if(chat_room_name!="\0")
        {
          std::string t = BackWindow("","Enter Chatroom Name",0);
          current_chatroom_name = t;
          t = "!"+ t;
          chat_room_name = "\0";
          c.write(string_to_msg(t));
        }
        c.build_chat_screen();
        c.build_text_box();
        c.display_msg("Changed Chatroom.");
        c.send_recent_messages();
        room_exists=0;
        current_chatroom = temp;
        continue;
      }
      //Checking if the user wants to delete any chatroom
      if(strcmp(line,"/delete chatroom") == 0)
      {
        //Sending message to the server to provide with the list of chatrooms
        c.write(string_to_msg("LOR"));
        //Infinite loop till the server responds (waiting for the server to respond)
        while(list_of_chatrooms == "\0") {}
        clear();
        refresh();
        //Prompting the user to enter the number of the chatroom they want to delete.
        std::string number = BackWindow("Enter Chatroom Number",list_of_chatrooms,1);
        list_of_chatrooms = "\0";
    
        c.refresh_all();
        c.send_recent_messages();
        /*
          If the number entered by the user is valid then a message is sent to the server to delete the chatroom
          otherwise an error message is displayed to the user.
          Anybody can delete a existing chatroom as long as there is no body in the chatroom.
        */
        try
        {
          temp = std::stoi(number);
        }
        catch(const std::exception& e)
        {
          c.display_msg("Invalid Number.");
          continue;
        }
        if(temp == 0)
        {
          c.display_msg("Cannot delete Main Lobby.");
          continue;
        }
        if(temp>9)
        {
          c.display_msg("Cannot delete a Chatroom which does'nt exist.");
          continue;
        }
        std::string buffer = "*" +std::to_string(temp);
        c.write(string_to_msg(buffer));
        while(delete_chat == 0) {}
        if(delete_chat == 1)
        {
         c.display_msg("Error: Did'nt Delete Chatroom. Maybe there are some clients in the chatroom or it doesnt exist.");
        }
        else
        {
          c.display_msg("Deleted Chatroom.");
        }
        delete_chat =0;
        continue;
      }
      if(strcmp(line, "/ban") == 0)
      {
        clear();
        refresh();
        /*
          Prompting the user to enter the nickname of the user they want to ban,
          The code doesn't check to make sure if the user with that name exists,
          it just blocks all messages recived from that user.
        */
        std::string user_to_ban = BackWindow("","Enter the nickname of the user you want to ban: " ,0);
        c.ban_user(n_name, user_to_ban);
        c.refresh_all();
        continue;
      }
      if(strcmp(line, "/unban") == 0)
      {
        clear();
        refresh();
        //Prompting the user to enter the nickname of the user they want to unban
        //If the nickname they enter is not on the ban list a error message is displayed.
        std::string user_to_unban  = BackWindow("","Enter the nickname of the user you want to unban: ",0);;
        c.unban_user(n_name, user_to_unban);
        c.refresh_all();
        continue;
      }
      if(strcmp(line,"/help") == 0)
      {
        WINDOW *helpwin = newwin(height/2,width/2,height/4,width/4);
        wprintw(helpwin,"\n");
        wprintw(helpwin," %15s : creates a chatroom.\n","/change chatroom");
        wprintw(helpwin," %15s : deletes a chatroom.\n","/delete chatroom");
        wprintw(helpwin," %15s  : quits the program.\n","/quit");
        wprintw(helpwin," %15s  : Bans a user.\n","/ban");
        wprintw(helpwin," %15s  : Unbans a user.\n","/unban");
        wprintw(helpwin," Press anything to continue.");
        box(helpwin,0,0);
        wgetch(helpwin);
        delwin(helpwin);
        
        c.refresh_all();
        continue;
      }
      if(strcmp(line,"/quit") == 0)
        break;
      if(std::strlen(line)==0)
        continue;
      
      //Just a regular message
      my_time = time(NULL);
      struct tm *time_data = localtime(&my_time);
      std::string hour = std::to_string(time_data->tm_hour);
      std::string min = std::to_string(time_data->tm_min);
      if(time_data->tm_hour < 10)
        hour = "0"+hour;
      if(time_data->tm_min < 10)
        min = "0"+min;
      //All the information is saved in variable fullline (including the name of the user, message, time)
      //This message is then sent to the server to distribute.
      std::string fullline = n_name + " [" + hour+":"+min + "] : " + line; 
      msg = string_to_msg(fullline);
      c.write(msg);
      c.refresh_all();
    }
    endwin();
    c.close();
    t.join();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
