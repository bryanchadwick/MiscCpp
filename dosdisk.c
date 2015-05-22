//  *********************************
//  *  Dos Disk Utility : dosdisk.c
//  *  Bryan Chadwick
//  *
//  *  For the navigation, copying and viewing of files
//  *    from DOS (FAT12) disks on UNIX type platforms (eg. Solaris)

//  *  Build with: gcc -o dosdisk dosdisk.c

#include <fcntl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define SECTOR_SIZE    512
#define ROOT_SIZE      14
#define FAT_SIZE       9
#define DIR_CLUST      4
#define DIR_SIZE       DIR_CLUST*SECTOR_SIZE
#define FAT_OFFSET     19
#define CLUSTER_OFFSET 31

#define ushort unsigned short
#define ulong  unsigned long
#define uchar  unsigned char
#define LOGO 0
#define to_lower(c) (('A' <= c && c <= 'Z')?(c -'A'+'a'):c)
#define to_upper(c) (('a' <= c && c <= 'z')?(c -'a'+'A'):c)

// ** Funcions for Big to Little Endian conversions (Intel to Sparc)
static inline ulong revl(ulong c){
  return c;//((c>>24)|((c&0xFF0000)>>8)|(c<<24)|((c&0xFF00)<<8));
}
 
static inline ushort revs(ushort c){
  return c;//((c<<8)|(c>>8));
}

// ** Menu options
enum {LIST = 'l',COPY = 'c',READ = 'd',INFO = 'i',
      QUIT = 'q',ENTER = 'e',HELP = 'h'};

// ** Disk structures
typedef struct dattr{
  uchar res:2,arc:1,dir:1,vol:1,sys:1,hid:1,rdo:1;
} dattr;

typedef struct dtime{
  ushort hrs : 5; /* low 5 bits: 2-second increments */
  ushort min : 6; /* middle 6 bits: minutes */
  ushort sec : 5; /* high 5 bits: hours (0-23) */
} dtime;

typedef struct ddate{
  ushort yrs : 7; /* low 5 bits: date (1-31) */
  ushort mon : 4; /* middle 4 bits: month (1-12) */
  ushort day : 5; /* high 7 bits: year - 1980 */
} ddate;

struct dirent{
  uchar  name[8],ext[3];
  struct dattr attrib;
  uchar  reserved,ctime_ms;
  struct dtime ctime;
  struct ddate cdate;
  struct ddate adate;
  ushort clust1;
  struct dtime mtime;
  struct ddate mdate;
  ushort clust0;
  ulong  size;
} dirent;

// ** Functions
char* getUnixName(char* name,int,struct dattr);
char* getVfatName(char* ent, struct dattr a);
char* getDosName(char* name);
char* getCommand(char* command);
struct dirent* searchDir(char*);
ushort getFatValue(long clust);
void doList(char* comm);
void doInfo(char* comm);
void doCopy(char* comm);
void doRead(char* comm, long);
void doEnter(char* comm);
void doHelp(char* comm);

uchar BOOT[SECTOR_SIZE];
uchar FAT[SECTOR_SIZE * FAT_SIZE * 2];
uchar ROOT[SECTOR_SIZE * ROOT_SIZE];
uchar CDIR[SECTOR_SIZE * DIR_CLUST];
uchar TDIR[SECTOR_SIZE * DIR_CLUST];
uchar* TWD;
uchar* CWD;
char  CWD_NAME[256] = "/";
char  TWD_NAME[256] = "";
char* DISK_NAME;
long  DISK_FILE;

int main(int argc, char** argv)
{
  uchar args = 0;
  char* argp;
  char *comm, command[128] = "";
  
  if(LOGO)printf("\n       ***  Bryans DOS Disk Helper ***\n");

  if(argc < 2){
    printf("  ** usage : %s [-ao] <Disk File>\n", argv[0]);
    return 1;
  }
 
  if(argv[1][0] == '-'){
    argp = argv[1];
    while(*++argp){
      switch(*argp){
      case 'o':args |= 1;break;
      case 'a':args |= 2;break;
      default:printf("  ** bad option, must be \'o\' or \'a\'\n");return 1;
      }
    }
    if(args & 2)
      DISK_NAME = "/dev/fd0";
    else
      DISK_NAME = argv[2];
  }else
    DISK_NAME = argv[1];
 
  DISK_FILE = open(DISK_NAME,O_RDONLY);
  if(!DISK_FILE || DISK_FILE < 0){
    printf("  ** Bad Disk Given : \"%s\" \n  ** Returned : %lu\n\n",DISK_NAME,DISK_FILE);
    return 1;
  }
  
  printf("\n   * Reading Boot Sector");fflush(stdout);
  read(DISK_FILE,BOOT,SECTOR_SIZE);
  if(!(args&1) && (BOOT[511] != 0xAA || BOOT[510] != 0x55)){
    printf("  ** Wrong Boot Sector Flag 0x%x : 0x%x\n",BOOT[510],BOOT[511]);
    printf("\n  ** Use -o to override !!!\n");
    goto END;
  }
 
  printf("\n   * Reading FATs");fflush(stdout);
  read(DISK_FILE,FAT,SECTOR_SIZE*FAT_SIZE*2);
 
  printf("\n   * Reading Root Dir\n");fflush(stdout);
  read(DISK_FILE,ROOT,SECTOR_SIZE*ROOT_SIZE);

  CWD = ROOT;
  TWD = TDIR;
 
  for(;;){
    comm = getCommand(command);
    switch(*comm){
    case  LIST: doList(++comm);break;
    case  COPY: doCopy(++comm);break;
    case  INFO: doInfo(++comm);break;
    case ENTER: doEnter(++comm);break;
    case  READ: doRead(++comm,1);break;
    case  HELP: doHelp(++comm);break;
    case  QUIT: goto END;
    case  '\n': break;
    default: printf("BAD COMMAND\n");
    }
  }  
 END: 
  printf("\n");
  close(DISK_FILE); 
  return 0;
}

char *getCommand(char *command)
{
  printf(" %s%s # ",DISK_NAME,CWD_NAME);
  fflush(stdout);
  read(0,command,64);
  return command;
}

void doList(char* comm)
{
  struct dirent *ent;
  struct ddate tdate;
  struct dtime ttime;
  ushort temp;
  ulong templ;
 
  ent = (struct dirent*)CWD;
  if(*comm == 't')
    ent = (struct dirent*)TWD;
  if(*comm == 'f'){
    write(1,CWD,512);
    return;
  }
  while(ent->name[0]){
    if(ent->name[0] != 0xE5 &&
       *(uchar*)(&ent->attrib) != 0xF){
      printf("  %c%c%c%c%c ",
             (ent->attrib.dir)?'d':'-',    // Dir
             (!(ent->attrib.hid))?'r':'-', // Hidden
             (!(ent->attrib.rdo))?'w':'-', // Read only
             ((ent->attrib.arc))?'a':'-',  // "Archive"
             ((ent->attrib.sys))?'s':'-'); // System
      temp = revs(*(short*)(&ent->mdate));
      tdate = *(struct ddate*)(&temp);
      temp = revs(*(short*)(&ent->mtime));
      ttime = *(struct dtime*)(&temp);
      printf(" %02d/%02d/%04d  %02d:%02d ",
             tdate.mon,tdate.day,tdate.yrs+1980,
             ttime.hrs,ttime.min);
      temp = revs(ent->clust0);
      templ= revl(ent->size);
    
      printf("%6ld  [%04d] ", templ, temp);
      fflush(stdout);
      write(1,getUnixName(ent->name,13,ent->attrib),13);
      printf(" %s\n",getVfatName(ent->name,ent->attrib));
    } 
    ent++;
  }
}

char *getUnixName(char* ent,int width,struct dattr a)
{
  static char out[64];
  char *outp = out,
    count = 0;
 
  while(*ent != ' ' && count < 8){
    *outp++ = to_lower(*ent);ent++;count++;
  }
  ent += 8 - count;
  if(*ent != ' ')
    *outp++ = '.';
  count = 0;
  while(*ent != ' ' && count++ < 3){
    *outp++ = to_lower(*ent);ent++;
  }
  if(a.dir)*outp++ = '/';
  while(outp < out+width)
    *outp++ = ' ';
  return out;  
}


struct long_name_ent{
  uchar  number;
  uchar name0[10];
  uchar  attr;
  uchar  type;
  uchar  checksum;
  uchar name1[12];
  ushort clust;
  uchar name2[4];
}long_name_ent;
 
char *getVfatName(char* ent, struct dattr a)
{
  static char out[256];
  char *outp = out;
  int i;
  struct long_name_ent *lent = (struct long_name_ent *)ent;
 
  lent--;
  while(lent->attr == 0xF){
    for(i = 0; i < 10; i+=2)
      *outp++ = lent->name0[i];
    for(i = 0; i < 12; i+=2)
      *outp++ = lent->name1[i];
    for(i = 0; i < 4; i+=2)
      *outp++ = lent->name2[i];
     
    if(lent->number & 0x40)
      break;
    lent--;
  }
  outp = out;
  while(*outp > 0)outp++;
 
  if(a.dir)
    *outp++ = '/';
  *outp = 0;
  return out;  
}

void doCopy(char* comm)
{
  char *in = comm,*out;
  long file;
 
  while(*++comm != ' ' && *comm != '\n');
  if(*comm == '\n'){
    printf("  ** no output file given\n");
    return;
  }
  *comm = 0;
  out = ++comm;
  while(*comm != '\n')
    comm++;
  *comm = 0;
  file = open(out,6);
  if(file < 0 || file == 0){
    printf("  ** bad output file given\n");
    return;
  }
  doRead(in,file);
  close(file);
}

void doRead(char* comm, long file)
{
  long clust,size;
  uchar sect[SECTOR_SIZE];
  struct dirent *ent;
  ent = searchDir(++comm);
  if((long)ent == -1)printf("  ** file not found\n");
  if((long)ent == -2)printf("  ** non-dir in path\n");
  if((long)ent < 0)return;
  if(ent->attrib.dir){
    printf("  ** it's a directory\n");
    return;
  } 

  clust = revs(ent->clust0);
  size  = revl(ent->size);
    
  while(clust > 0 && clust < 0xFF8 && size > 0){
    lseek(DISK_FILE,(clust+CLUSTER_OFFSET)*SECTOR_SIZE,SEEK_SET);
    read(DISK_FILE,sect,SECTOR_SIZE);
    write(file,sect,SECTOR_SIZE);
    size -= SECTOR_SIZE;
    clust = getFatValue(clust);
  }
}

void doEnter(char* comm)
{
  char *temp = ++comm;
  struct dirent *ret;
  long ind,clust;
 
  while(*comm != '\n' && *comm != ' ')
    comm++;
  *comm = 0;
 
  if(*temp == '/' && *(temp+1)==0)
    clust = 0;
  else{
    ret = searchDir(temp);
    if((long)ret == -1)printf("\n  ** dir not found\n");
    if((long)ret == -2)printf("\n  ** non-dir in path\n");
    if((long)ret < 0)return;
    if(!ret->attrib.dir){printf("\n  ** file not a dir\n");return;}

    clust = revs(ret->clust0);
  }
 
  if(!clust){
    CWD = ROOT;
    CWD_NAME[0] = '/';
    CWD_NAME[1] = 0;
    return;
  }
  
  ind = 0;
  while(clust > 0 && clust < 0xFF8 && ind < DIR_SIZE){
    lseek(DISK_FILE,(clust+CLUSTER_OFFSET)*SECTOR_SIZE,SEEK_SET);
    read(DISK_FILE,CDIR+ind,SECTOR_SIZE);
    clust = getFatValue(clust);
    ind += SECTOR_SIZE;
  }
  CWD = CDIR;
  strcpy(CWD_NAME,TWD_NAME);
}

void doHelp(char* comm)
{
  if(LOGO)printf("\n       ***  Bryans DOS Disk Helper ***\n\n");
  printf("                     * Commands * \n");
  printf("    l               : List the CWD, Unix long type listing\n");
  printf("    e <dir>         : Enter a director\n");
  printf("    d <file>        : Display a file, just like Unix \"cat\"\n");
  printf("    c <file> <copy> : Copy a file, to your Unix CWD\n");
  printf("    h               : Print this help\n");
  printf("    q               : Quit\n\n");
}

char* getDosName(char* name)
{
  static char out[16];
  char* outp, *inp;
 
  inp = name; 
  outp = out;
 
  if(*inp == '.'){
    *outp++ = *inp++;
    if(*inp == '.')
      *outp++ = *inp++;
  }else{ 
    while(*inp == ' ')inp++;
    while(*inp && *inp != '\n' &&
          *inp != '.' && inp < name+8){
      *outp++ = to_upper(*inp);inp++;
    }
    while(outp < out+8)
      *outp++ = ' ';
    if(*inp == '.'){
      inp++;
      while(*inp && outp < out+11 && inp < name+12){
        *outp++ = to_upper(*inp);inp++;
      }
    }
  }  
  while(outp < out+11)
    *outp++ = ' ';
  *outp = 0;
  return out;
}

struct dirent *searchDir(char* start)
{
  char *test,*src,*name = start,
    *namep,*namesrc,
    done = 0,found = 0,
    *twdp = TWD_NAME;
  struct dirent* ent;
  long clust,ind;
 
  if(*name == '/'){
    TWD = ROOT;
    *twdp++ = '/';
    name++;
  }else{
    TWD = CWD;
    test = CWD_NAME;
    while(*test)
      *twdp++ = *test++;
  }
  
  namep = name;
  while(!done){ // Find the next path element
    found = 0;
    ent = (struct dirent *)TWD;
    name = namep;
    while(*namep && *namep != '/' && *namep != ' ' && *namep != '\n')
      namep++;
    done = (*namep != '/');
    *namep++ = 0;
   
    namesrc = getDosName(name);
   
    while(ent->name[0] && !found){
      ent->reserved = 0;
      if(ent->name[0] != 0xE5 && *(uchar*)(&ent->attrib) != 0xF){
        test = ent->name;
        src = namesrc;
        while(*test == *src && src < namesrc + 11){
          test++;src++;
        }
        if((found = (src == namesrc + 11)))
          break;
      }
      ent++;
    } 
    
    if(!found)return (struct dirent *)-1; 
   
    if(ent->attrib.dir){
      if(ent->name[0] == '.'){
        if(ent->name[1] == '.'){
          twdp--;
          while(*(twdp-1) != '/')twdp--;
        }
      }else{ // Add new Dir Name
        test = getUnixName(ent->name,12,ent->attrib);
        while(*test != ' ')
          *twdp++ = *test++;
      }
      *twdp = 0;
    }
    if(done)return (ent); 
    if(!ent->attrib.dir)return (struct dirent *)-2;
   
    clust = revs(ent->clust0);
    ind = 0;
    // NotDone, get next dir to search
    
    while(clust > 0 && clust < 0xFF8 && ind < DIR_SIZE){
      lseek(DISK_FILE,(clust+CLUSTER_OFFSET)*SECTOR_SIZE,SEEK_SET);
      read(DISK_FILE,TDIR+ind,SECTOR_SIZE);
      clust = getFatValue(clust);
      ind += SECTOR_SIZE;
    }
    TWD = TDIR;
  }
  return (struct dirent*)-3; 
}

ushort getFatValue(long clust)
{
  long  ind = clust+(clust>>1);
  ushort val;
 
  if(clust > 2847)return 0;
  val = FAT[ind] | (FAT[ind+1] << 8);
  return ((clust%2)?(val>>4):(val&0xFFF));
}

void doInfo(char* comm)
{
  struct dirent *ent = searchDir(++comm);
  long templ;
 
  if((long)ent < 0){
    write(1, "  ** file not found\n", 20);
    return;
  } 
  printf("  ** %s ", getUnixName(ent->name,12,ent->attrib));
  templ = revl(ent->clust0);
  printf("-> %ld", templ);
  while(templ > 0 && templ < 0xFF8){
    templ =  getFatValue(templ); 
    printf("-> %ld", templ);
  }
  printf("\n");
}


 
