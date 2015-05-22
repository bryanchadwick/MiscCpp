//  *********************************
//  *  FTP-like service : ftpd.cpp
//  *  Bryan Chadwick
//  *  
//  *  Simple server waiting for requests, forwards files for download and/or
//  *    html-link directory listings

//  *  Build with: g++ -o ftpd ftpd.cpp

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <iostream>

#define MAX_LEN  1024  // Length of Path Strings
#define PORT_NUM 5001  // Listening Port

// simple macro, no lib. precedure
#define is_letter(c) (((c) >= 'a' && (c) <= 'z') || \
                      ((c) >= 'A' && (c) <= 'Z'))

// Forward Declarations
void quit(int signum);
void die(const char* message, int );
int create_socket(int port);
void print_address(const char* prefix, struct sockaddr_in& socket_addr);
void write_header(int socket, const char* plain_or_html);
void write_dir(int socket, const char* path);
void write_file(int socket, const char* path);
void write_const(int socket, const char* text);

// Global, so we can close if/when we quit
int my_socket = 0;

// Main == infinite accept loop
int main()
{
  // Unix socket address
  struct sockaddr_in conn_addr;
  // Directory/File Stat-ing
  struct stat entstat;
  // Buffers and paths (half for dir, half for file)
  char recv_buf[MAX_LEN], base_dir[MAX_LEN/2], file_name[MAX_LEN/2];

  // Catch Ctrl-C
  signal(SIGINT, quit);

  // Find Base (where we started)
  getcwd(base_dir, MAX_LEN/2);
  std::cout << "CWD : \"" << base_dir << "\"\n";
  
  my_socket = create_socket(PORT_NUM);
  while(1){
    fflush(stdout);  // Make sure messages from above get to output incase of a crash

    // Setup to listen on the Socket
    socklen_t length = sizeof(struct sockaddr_in);
    
    int temp_socket = accept(my_socket, (struct sockaddr*)&conn_addr, &length);
    print_address("Connection from", conn_addr);

    if(temp_socket < 0)
      die("Connection Error", temp_socket);

    length = recv(temp_socket, recv_buf, MAX_LEN, 0);
    if(length <= 0){
      std::cerr << "** Transmission Error\n";
      close(temp_socket);
      continue;
    }

    char *start = recv_buf;       // Start of the request
    while(*start == ' ')start++;  // Skip whitespace
    
    // Only handle [G]ET
    if(*start == 'G'){
      while(*start != '/')start++;// Start of filename to get
      char* end = start;          // Find the end...
      while(*end != ' ')end++;    // filename == chars recv_buf[mark] through recv_buf[mark2-1]
      *end = 0;

      // File requested
      sprintf(file_name, "%s%s", base_dir, start);
      std::cout << "  * REQUEST : GET " << file_name << std::endl;
      
      // Handle Files and Dirs
      if(!stat(file_name, &entstat)){
        if(S_ISDIR(entstat.st_mode)){
          std::cout << "  * RESPONSE : Dir\n";
          write_dir(temp_socket, file_name);
        }else{
          std::cout << "  * RESPONSE : File\n";
          write_file(temp_socket, file_name);
        }
      }else{
        std::cout << "  * RESPONSE : Not Found\n";
        write_header(temp_socket, "html");
        write_const(temp_socket, "<html><center><h1>FILE NOT FOUND</center></h1></html>");
      }
    }
    close(temp_socket);
    std::cout << "  * RESPONSE : Done\n";
  }
}

void quit(int signum){
  std::cout << "\n  * Quitting\n";
  if(my_socket != 0)
    close(my_socket);
  exit(0);
}

int create_socket(int port){
  int my_socket = socket(AF_INET, SOCK_STREAM, 0);
  if(my_socket < 0)
    die("Couldn\'t Get New Socket", my_socket);

  struct sockaddr_in my_addr;
  my_addr.sin_family = AF_INET;
  my_addr.sin_port = htons(port);
  my_addr.sin_addr.s_addr = INADDR_ANY;

  if(bind(my_socket, (struct sockaddr*)&my_addr, sizeof(my_addr)) < 0)
    die("Couldn\'t Bind Socket to Port", 0);
  if(listen(my_socket, 5) < 0)
    die("Couldn\'t Listen on Port", 0);

  print_address("Setup On", my_addr);
  return my_socket;
}

void print_address(const char* prefix, struct sockaddr_in& socket_addr){
  unsigned long addr = socket_addr.sin_addr.s_addr;
  std::cout << prefix << " : " << (addr&0xFF) << "." << ((addr>>8)&0xFF) << "."
            << ((addr>>16)&0xFF) << "." << ((addr>>24)&0xFF) << ":" << socket_addr.sin_port
            << std::endl;
}

void die(const char* message, int err){
  std::cerr << "** ERROR #" << err << ": " << message << " !!\n";
  exit(1);
}

void write_header(int socket, const char* plain_or_html){
  char hdr[256];
  sprintf(hdr, "HTTP/1.1 200 OK\r\nContent-Type: text/%s\r\n\r\n", plain_or_html);
  write(socket, hdr, strlen(hdr));
}

void write_const(int socket, const char* text){
  write(socket, text, strlen(text));
}

void write_dir(int socket, const char *dir_name){
  char buffer[4096], full_path[1024];
  struct dirent* entry;
  struct stat entstat;

  DIR* stream = opendir(dir_name);
  write_header(socket, "html");

  sprintf(buffer, "<html><head><title>Test%s</title><head><center><h1>%s</h1></center><br/>\n<ul>\n", dir_name, dir_name);
  write(socket, buffer, strlen(buffer));
  
  while((entry = readdir(stream))){
    sprintf(full_path, "%s%s", dir_name, entry->d_name);

    stat(full_path, &entstat);
    sprintf(buffer, "<li><a href=\"%s\">%s%s</a></li>\n",
            entry->d_name, entry->d_name, (S_ISDIR(entstat.st_mode) ? "/" : ""));
    write(socket, buffer, strlen(buffer));
  }
  write_const(socket, "</ul></html>");
}

void write_file(int socket, const char *path)
{
  write_header(socket, "plain");

  int file = open(path, O_RDONLY), len;
  char buffer[MAX_LEN];
  while(len = read(file, buffer, MAX_LEN))
    write(socket, buffer, len);
  close(file);
}
