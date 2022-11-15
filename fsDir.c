#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "fsDir.h"
#include "fsLow.h"
#include "mfs.h"
#include "fsFree.h"
#include "fsHelpers.h"

#define DE_COUNT 64		  // initial number of d_entries to allocate to a directory
#define PATH_LENGTH 1024 // initial path length

// Initialize a directory
// If parent_loc == 0, this is the root directory
int init_dir(int parent_loc)
  {
  printf("***** init_dir *****\n");

  int num_blocks = get_num_blocks(sizeof(DE) * DE_COUNT, fs_vcb->block_size);
  printf("Number of blocks in dir: %d\n", num_blocks);
  int num_bytes = num_blocks * fs_vcb->block_size;
  printf("Number of bytes in dir: %d\n", num_bytes);
  printf("Size of DE: %lu\n", sizeof(DE));
  DE* dir_array = malloc(num_bytes);
  int dir_loc = alloc_free(num_blocks);
  printf("Directory Location: %d\n", dir_loc);

  if (dir_loc == -1)
    {
    perror("Allocation of directory failed.\n");
    free(dir_array);
    dir_array = NULL;
    return -1;
    }

  // track number of blocks root directory occupies
  fs_vcb->root_blocks = num_blocks;

  // Directory "." entry initialization
  dir_array[0].size = num_bytes;
  dir_array[0].loc = dir_loc;
  time_t curr_time = time(NULL);
  dir_array[0].created = curr_time;
  dir_array[0].modified = curr_time;
  dir_array[0].accessed = curr_time;
  strcpy(dir_array[0].attr, "d");
  strcpy(dir_array[0].name, ".");

  // Parent directory ".." entry initialization
  // If parent_loc == 0, this is the root directory
  if (parent_loc == 0)
    {
    dir_array[1].size = num_bytes;
    dir_array[1].loc = dir_loc;        // currently the only difference
    dir_array[1].created = curr_time;
    dir_array[1].modified = curr_time;
    dir_array[1].accessed = curr_time;
    }
  else
    {
    // Need to LBAread the parent to get all the data about the parent
    // Is this even necessary?
    dir_array[1].size = num_bytes;
    dir_array[1].loc = parent_loc;     // currently the only difference
    dir_array[1].created = curr_time;
    dir_array[1].modified = curr_time;
    dir_array[1].accessed = curr_time;
    }
  strcpy(dir_array[1].attr, "d");
  strcpy(dir_array[1].name, "..");

  for (int i = 2; i < DE_COUNT; i++)
    {
    // Directory "." entry initialization
    // dir_array[i].size = 0;
    // dir_array[i].loc = 0;
    // dir_array[i].created = 0;
    // dir_array[i].modified = 0;
    // dir_array[i].accessed = 0;
    strcpy(dir_array[i].attr, "a");
    // strcpy(dir_array[i].name, "");
    }

  print_de(&dir_array[0]);
  print_de(&dir_array[1]);

  int blocks_written = LBAwrite(dir_array, num_blocks, dir_loc);

  if (blocks_written != num_blocks)
    {
    perror("LBAwrite failed\n");
    return -1;
    }

  printf("LBAwrite blocks written: %d\n", blocks_written);

  // set the current working directory to root if initializing root
  if (parent_loc == 0)
    {
    // malloc then copy newly created root directory to current working
    // directory array for ease of tracking.
    cw_dir_array = malloc(num_bytes);
    memcpy(cw_dir_array, dir_array, num_bytes);

    // malloc then set the path to root.
    cw_path = malloc(PATH_LENGTH);
    strcpy(cw_path, "/");
    }
  
  free(dir_array);
  dir_array = NULL;
          
  return dir_loc;
  }

// parsePath returns NULL if the path is invalid (e.g. one of the directories
// does not exist) or the n-1 directory (array of DE's) pointer
DE* parsePath(char *pathname)
  {
  printf("***** parsePath *****\n");

  int num_blocks = get_num_blocks(sizeof(DE) * DE_COUNT, fs_vcb->block_size);
  int num_bytes = num_blocks * fs_vcb->block_size;
  DE* dir_array = malloc(num_bytes);  // malloc space for a directory

  if (pathname[0] == '/')
    {
    LBAread(dir_array, fs_vcb->root_blocks, fs_vcb->root_loc);
    }
  else
    {
    memcpy(dir_array, cw_dir_array, num_bytes);
    }

  // malloc plenty of space for token array
  char **tok_array = malloc(strlen(pathname) * sizeof(char*));
  char *lasts;
  char *tok = strtok_r(pathname, "/", &lasts);
  tok_array[0] = tok;
  int tok_count = 1;

  while (tok != NULL)
    {
    tok = strtok_r(NULL, "/", &lasts);
    tok_array[tok_count++] = tok;
    }

  for (int i = 0; i < tok_count - 1; i++)
    {
    int found = 0;
    for (int j = 0; j < DE_COUNT; j++)
      {
      if (strcmp(tok_array[i], dir_array[j].name) == 0)
        {
        found = 1;
        LBAread(dir_array, num_blocks, dir_array[j].loc);
        break;
        }
      }
    if (!found) 
      {
      return NULL;
      }
    }

  return dir_array;
  }

void print_dir(DE* dir_array)
  {
  printf("=================== Printing Directory Map ===================\n");
  printf("Directory Location: %d\n\nindex  Size    Loc     Att\n", dir_array[0].loc);
  for (int i = 0; i < DE_COUNT; i++)
    {
    printf("%2d     %#06x  %#06x  %s\n", i, dir_array[i].size, dir_array[i].loc, dir_array[i].attr);
    }
  }

void print_de(DE* dir)
  {
  printf("=================== Printing Directory Entry ===================\n");
  printf("Size: %d\nLocation: %d\nCreated: %ld\nModified: %ld\nAccessed: %ld\nAttribute: %s\nName: %s\n",
          dir->size, dir->loc, dir->created, dir->modified, dir->accessed, dir->attr, dir->name);
  }
