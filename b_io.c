/**************************************************************
* Class:  CSC-415-02 Fall 2022
* Names: Mark Kim, Peter Truong, Chengkai Yang, Zeel Diyora
* Student IDs: 918204214, 915780793, 921572896, 920838201
* GitHub Name: mkim797
* Group Name: Diligence
* Project: Basic File System
*
* File: b_io.c
*
* Description: Basic File System - Key File I/O Operations
*
**************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>			// for malloc
#include <string.h>			// for memcpy
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "b_io.h"
#include "fsDir.h"
#include "mfs.h"
#include "fsLow.h"
#include "fsHelpers.h"
#include "fsFree.h"

#define MAXFCBS 20

// file control block buffer struct
typedef struct b_fcb
	{
  DE * fi;	      // holds the low level systems file info
  DE * dir_array; // holds the directory array where the file resides
	char * buf;     // buffer for open file
	int bufOff;		  // current offset/position in buffer
	int bufLen;		  // number of bytes in the buffer
	int curBlock;	  // current file system block location
  int blockIndex; // current block index
  int fileIndex;  // index of file in dir_array
  int accessMode; // file access mode
  // int debugFlag;  // debugFlag to stop writing/reading (only for debugging)
	} b_fcb;
	
b_fcb fcbArray[MAXFCBS];

int startup = 0;	//Indicates that this has not been initialized

//Method to initialize our file system
void b_init ()
	{
	//init fcbArray to all free
	for (int i = 0; i < MAXFCBS; i++)
		{
		fcbArray[i].buf = NULL; //indicates a free fcbArray
		}
		
	startup = 1;
	}

//Method to get a free FCB element
b_io_fd b_getFCB ()
	{
	for (int i = 0; i < MAXFCBS; i++)
		{
		if (fcbArray[i].buf == NULL)
			{
			return i;		//Not thread safe (But do not worry about it for this assignment)
			}
		}
	return (-1);  //all in use
	}
	
// Interface to open a buffered file
// Modification of interface for this assignment, flags match the Linux flags for open
// O_RDONLY, O_WRONLY, or O_RDWR
b_io_fd b_open (char * filename, int flags)
	{
	if (startup == 0) b_init();  //Initialize our system
  
  // retrieve directory array pointer
  DE *dir_array = parsePath(filename);

  // get the last token of the path
	char *last_tok = get_last_tok(filename);

  // retrieve the index of the directory array of the last token in path
	int found_index = get_de_index(last_tok, dir_array);

  // check all invalid cases if no file/directory found
  if (found_index < 0)
    {
    int error_encountered = 0;
    // cannot read only a file that does not exist 
    if (flags & O_RDONLY)
      {
      perror("b_open: read only: file not found\n");
      error_encountered = 1;
      }

    // cannot create a file if the create file is not set
    if (!(flags & O_CREAT))
      {
      perror("b_open: create flag not set; new file cannot be created\n");
      error_encountered = 1;
      }

    if (error_encountered)
      {
      free(dir_array);
      dir_array = NULL;
      free(last_tok);
      last_tok = NULL;
      
      return -2;
      }
    }
  else
    {
    // check if the DE is of a directory
    if (dir_array[found_index].attr == 'd')
      {
      printf("b_open: '%s' is a directory and cannot be opened as a file\n",
            dir_array[found_index].name);

      free(dir_array);
      dir_array = NULL;
      free(last_tok);
      last_tok = NULL;
        
      return -2;
      }
    }

  // allocate the file system buffer sized to the file system block size
	char *buf = malloc(fs_vcb->block_size);
	if (buf == NULL)
		{
    perror("b_open: buffer malloc failed\n");
    free(dir_array);
    dir_array = NULL;
    free(last_tok);
    last_tok = NULL;
    
		return (-1);
		}
	
	b_io_fd returnFd = b_getFCB();	// get our own file descriptor
									                // check for error - all used FCB's
  if (returnFd == -1)
    {
    perror("b_open: no free file control blocks available\n");
    
    free(dir_array);
    dir_array = NULL;
    free(last_tok);
    last_tok = NULL;
    
    return returnFd;
    }

  // malloc file directory entry
  fcbArray[returnFd].fi = malloc(sizeof(DE));

  if (fcbArray[returnFd].fi == NULL)
    {
    perror("b_open: fcbArray file info malloc failed\n");

    free(dir_array);
    dir_array = NULL;
    free(last_tok);
    last_tok = NULL;
    
    return (-1);
    }

  // if a file was found, load the directory entry into the fcbArray; otherwise,
  // create a new file
  if (found_index > -1)
    {
    memcpy(fcbArray[returnFd].fi, &dir_array[found_index], sizeof(DE));
    fcbArray[returnFd].fileIndex = found_index;
    }
  else
    {
    int new_file_index = get_avail_de_idx(dir_array);
    if (new_file_index == -1)
      {
      return -1;
      }
    
    int new_file_loc = alloc_free(DEFAULT_FILE_BLOCKS);
    if (new_file_loc == -1)
      {
      return -1;
      }

    // New directory entry initialization
    dir_array[new_file_index].size = 0;
    dir_array[new_file_index].num_blocks = DEFAULT_FILE_BLOCKS;
    dir_array[new_file_index].loc = new_file_loc;
    time_t curr_time = time(NULL);
    dir_array[new_file_index].created = curr_time;
    dir_array[new_file_index].modified = curr_time;
    dir_array[new_file_index].accessed = curr_time;
    dir_array[new_file_index].attr = 'f';
    strcpy(dir_array[new_file_index].name, last_tok);

    // write new empty file to disk
    write_all_fs(dir_array);

    // copy new directory entry to fcbArray file info
    memcpy(fcbArray[returnFd].fi, &dir_array[new_file_index], sizeof(DE));

    // print_de(fcbArray[returnFd].fi);

    fcbArray[returnFd].fileIndex = new_file_index;
    }

  // initialize fcbArray entry
  fcbArray[returnFd].dir_array = dir_array;
  fcbArray[returnFd].buf = buf;
  fcbArray[returnFd].bufOff = 0;
  fcbArray[returnFd].bufLen = 0;
  fcbArray[returnFd].blockIndex = 0;
  fcbArray[returnFd].curBlock = fcbArray[returnFd].fi->loc;
  fcbArray[returnFd].accessMode = flags;

  // Per man page requirements, O_TRUNC sets file size to zero;
  if (flags & O_TRUNC)
    {
    fcbArray[returnFd].fi->size = 0;
    }
  
	free(last_tok);
	last_tok = NULL;
	
	return (returnFd);
	}


// Interface to seek function	
int b_seek (b_io_fd fd, off_t offset, int whence)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1)
	if ((fd < 0) || (fd >= MAXFCBS))
		{
		return (-1); 					//invalid file descriptor
		}

  // Calculate block offset to retrieve the current block number
  int block_offset = offset / fs_vcb->block_size;

  // Set absolute block offset
  if (whence & SEEK_SET)
    {
    fcbArray[fd].curBlock = get_block(fcbArray[fd].fi->loc, block_offset);
    }

  // set relative block offset
  else if (whence & SEEK_CUR)
    {
    fcbArray[fd].curBlock = get_block(fcbArray[fd].curBlock, block_offset);
    }

  // set current block to the block following the end of file
  else if (whence & SEEK_END)
    {
    fcbArray[fd].curBlock = get_block(fcbArray[fd].fi->loc, fcbArray[fd].fi->num_blocks);
    }

  // set buffer offset to whatever remains after all the full blocks are
  // accounted for.
  fcbArray[fd].bufOff = offset % fs_vcb->block_size;
  fcbArray[fd].bufLen = 0;
  fcbArray[fd].fi->accessed = time(NULL);
	
	return fcbArray[fd].bufOff; //Change this
	}

// Interface to write function
// Based off of b_read as provided by Prof Bierman code in class
int b_write (b_io_fd fd, char * buffer, int count)
	{
  // if (fcbArray[fd].debugFlag)
  //   {
  //   return fcbArray[fd].debugFlag;  // stop writes once this flag is set
  //   }
	if (startup == 0) b_init();  //Initialize our system

  // should not happen, but checking anyways
	if (fcbArray[fd].accessMode & O_RDONLY)
	{
		perror("b_write: file does not have write access");
		return -1;
	}

	// check that fd is between 0 and (MAXFCBS-1) or if count < 0
	if ((fd < 0) || (fd >= MAXFCBS) || count < 0)
		{
		return (-1); 					//invalid file descriptor
		}

  // should not happen, but checking anyways
	if (fcbArray[fd].accessMode & O_RDONLY)
    {
    perror("b_write: file does not have write access");
    return -1;
    }
	
	// check to see if the fcb exists in this location
	if (fcbArray[fd].fi == NULL)
		{
		return -1;
		}

  // print_de(fcbArray[fd].fi);
  // print_fd(fd);

  // calculate if extra blocks are necessary (e.g. not enough empty blocks
  // available for a growing file)
  int extra_blocks = get_num_blocks(
    fcbArray[fd].fi->size + count + fs_vcb->block_size - (fcbArray[fd].fi->num_blocks * fs_vcb->block_size),
    fs_vcb->block_size);

	if (extra_blocks > 0)
		{
    // set the number of extra blocks to allocate to the larger of (extra_blocks)
    // or (fcbArray[fd].fi->num_blocks), essentially doubling the current number
    // of blocks
		extra_blocks = fcbArray[fd].fi->num_blocks > extra_blocks
                  ? fcbArray[fd].fi->num_blocks : extra_blocks;

    // allocate the extra blocks and save location
    int new_blocks_loc = alloc_free(extra_blocks);

    if (new_blocks_loc < 0)
      {
      perror("b_write: freespace allocation failed\n\n");
      return -1;
      }

    // set final block of file in the free space map to the starting block of
    // the newly allocated space and increment the number of blocks that the
    // file occupies
    freespace[get_block(fcbArray[fd].fi->loc, fcbArray[fd].fi->num_blocks - 1)] = new_blocks_loc;
    fcbArray[fd].fi->num_blocks += extra_blocks;
		}

  // if file is empty set file control block variables accordingly
	int availBytesInBuf;
  int bytesDelivered = 0;
  if (fcbArray[fd].fi->size < 1)
    {
    fcbArray[fd].fi->size = 0;
    availBytesInBuf = fs_vcb->block_size;
    }
  else
    {
    // available bytes in buffer
    availBytesInBuf = fs_vcb->block_size - fcbArray[fd].bufOff;
    }
	
  // split user's buffer into three parts: part1 is what is left in the buffer
  // (if any), part2 is the section of the buffer that occupies zero to many
  // entire blocks, and part3 is the remaining amount to partially fill the
  // buffer.
	int part1, part2, part3, numBlocksToCopy, blocksWritten;
	if (availBytesInBuf >= count)
		{
    // if the amount of data to write is less than the remaining amount in the
    // buffer, we just use the entire amount called for.
    part1 = count;
    part2 = 0;
    part3 = 0;
		}
  else
    {
    // the count is larger than what is left in the fcb buffer, so the part1
    // section is just what is left in buffer.
    part1 = availBytesInBuf;

    // set the part3 section to all the bytes left in the file
    part3 = count - availBytesInBuf;

    // divide the part3 section by the chunk size to get the total number of
    // complete blocks to copy and multiply by the chunk size to get the bytes
    // that those blocks occupy (for part2)
    numBlocksToCopy = part3 / fs_vcb->block_size;
    part2 = numBlocksToCopy * fs_vcb->block_size;

    // finally subtract the complete bytes in part2 from the total bytes left to
    // get the part3 bytes to put in buffer
    part3 = part3 - part2;
    }

  // part1 section
  if (part1 > 0)
    {
    // copy part1 of the user's buffer into the fcb buffer
    memcpy(fcbArray[fd].buf + fcbArray[fd].bufOff, buffer, part1);

    // THIS MIGHT BE ABLE TO BE OPTIMIZED, but won't try it now because it is working
    // write the entire block to disk
    blocksWritten = LBAwrite(fcbArray[fd].buf, 1, fcbArray[fd].curBlock);

    // must set buffer offset in case there is another call later
    fcbArray[fd].bufOff += part1;

    // determine if we reached the end of the block; if so, get the next block
    // and reset the fcb buffer offset back to zero.
    if (fcbArray[fd].bufOff >= fs_vcb->block_size)
      {
      fcbArray[fd].curBlock = get_next_block(fcbArray[fd].curBlock);
      fcbArray[fd].bufOff = 0;
      }
    }

  // write all the complete blocks from the user's buffer to disk
  if (part2 > 0)
    {
    blocksWritten = 0;
    
    // write each block one-by-one since we don't know where the next block will be
    for (int i = 0; i < numBlocksToCopy; i++)
      {
      blocksWritten += LBAwrite(buffer + part1 + (i * fs_vcb->block_size), 1, fcbArray[fd].curBlock);
      fcbArray[fd].curBlock = get_next_block(fcbArray[fd].curBlock);
      }
    part2 = blocksWritten * fs_vcb->block_size;  // number of bytes written
    }

  // write remaining partial block from user's buffer to disk
  if (part3 > 0)
    {
    // copy the user's buffer into the fcb buffer
    memcpy(fcbArray[fd].buf + fcbArray[fd].bufOff, buffer + part1 + part2, part3);

    // write entire block to disk
    blocksWritten = LBAwrite(fcbArray[fd].buf, 1, fcbArray[fd].curBlock);

    // must set buffer offset in case there is another call later
    fcbArray[fd].bufOff += part3;
    }

  // if the fcb buffer is full, get the next block and set the offset to zero
  if (fcbArray[fd].bufOff >= fs_vcb->block_size)
    {
    fcbArray[fd].curBlock = get_next_block(fcbArray[fd].curBlock);
    fcbArray[fd].bufOff = 0;
    }

  // THE FOLLOWING CAN BE OPTIMIZED -- PROBABLY CAN BE DONE IN b_close
  // set accessed/modified times and file size accordingly
  time_t cur_time = time(NULL);
  fcbArray[fd].fi->accessed = cur_time;
  fcbArray[fd].fi->modified = cur_time;
  bytesDelivered = part1 + part2 + part3;
  fcbArray[fd].fi->size += bytesDelivered;

  // copy changes to the fcb directory entry to the directory array containing
  // the file.
  memcpy(&fcbArray[fd].dir_array[fcbArray[fd].fileIndex], fcbArray[fd].fi, sizeof(DE));

  // write changes to the directory array to disk
  write_dir(fcbArray[fd].dir_array);

  return part1 + part2 + part3;
	}



// Interface to read a buffer

// Filling the callers request is broken into three parts
// Part 1 is what can be filled from the current buffer, which may or may not be enough
// Part 2 is after using what was left in our buffer there is still 1 or more block
//        size chunks needed to fill the callers request.  This represents the number of
//        bytes in multiples of the blocksize.
// Part 3 is a value less than blocksize which is what remains to copy to the callers buffer
//        after fulfilling part 1 and part 2.  This would always be filled from a refill 
//        of our buffer.
//  +-------------+------------------------------------------------+--------+
//  |             |                                                |        |
//  | filled from |  filled direct in multiples of the block size  | filled |
//  | existing    |                                                | from   |
//  | buffer      |                                                |refilled|
//  |             |                                                | buffer |
//  |             |                                                |        |
//  | Part1       |  Part 2                                        | Part3  |
//  +-------------+------------------------------------------------+--------+

// Based entirely off of Prof Bierman code shared in class
int b_read (b_io_fd fd, char * buffer, int count)
	{
	if (startup == 0) b_init();  //Initialize our system

	// check that fd is between 0 and (MAXFCBS-1) or if count < 0
	if ((fd < 0) || (fd >= MAXFCBS)) // || count < 0
    {
    return (-1); //invalid file descriptor
    }

	if (fcbArray[fd].accessMode & O_WRONLY)
    {
    perror("b_read: file does not have read access");
    return -1;
    }
	
	// check to see if the fcb exists in this location
	if (fcbArray[fd].fi == NULL)
		{
		return -1;
		}

  int totalBytesRead = fcbArray[fd].blockIndex * fs_vcb->block_size + fcbArray[fd].bufOff + 1;

  if (totalBytesRead >= fcbArray[fd].fi->size)
    {
    return -1;
    }

  // print_fd(fd);

	// available bytes in buffer
	int availBytesInBuf = fcbArray[fd].bufLen - fcbArray[fd].bufOff;
  // printf("Available bytes in buffer: %d\n", availBytesInBuf);

	// number of bytes already delivered
	int bytesDelivered = (fcbArray[fd].blockIndex * fs_vcb->block_size) - availBytesInBuf;

	// limit count to file length
	if ((count + bytesDelivered) > fcbArray[fd].fi->size)
		{
		count = fcbArray[fd].fi->size - bytesDelivered;

		if (count < 0)
			{
			printf("Error: Count: %d   Delivered: %d   CurBlock: %d", count,
							bytesDelivered, fcbArray[fd].curBlock);
			}
		}
	
	int part1, part2, part3, numBlocksToCopy, blocksRead;
	if (availBytesInBuf >= count)
		{
    // part1 is the part1 section of the data
    // it is the amount of bytes that will fill up the available bytes in buffer
    // if the amount of data is less than the remaining amount in the buffer, we
    // just copy the entire amount into the buffer.
    part1 = count;
    part2 = 0;
    part3 = 0;
		}
  else
    {
    // the file is too big, so the part1 section is just what is left in buffer
    part1 = availBytesInBuf;

    // set the part3 section to all the bytes left in the file
    part3 = count - availBytesInBuf;

    // divide the part3 section by the chunk size to get the total number of
    // complete blocks to copy and multiply by the chunk size to get the bytes
    // that those blocks occupy
    numBlocksToCopy = part3 / fs_vcb->block_size;
    part2 = numBlocksToCopy * fs_vcb->block_size;

    // finally subtract the complete bytes in part2 from the total bytes left to
    // get the part3 bytes to put in buffer
    part3 = part3 - part2;
    }

  // memcopy part1 section
  if (part1 > 0)
    {
    memcpy(buffer, fcbArray[fd].buf + fcbArray[fd].bufOff, part1);

    fcbArray[fd].bufOff += part1;
    }

  // LBAread all the complete blocks into the buffer
  if (part2 > 0)
    {
    blocksRead = 0;
    
    // read each block one-by-one since we don't know where the next block will be
    for (int i = 0; i < numBlocksToCopy; i++)
      {
      blocksRead += LBAread(buffer + part1 + (i * fs_vcb->block_size), 1, fcbArray[fd].curBlock);
      fcbArray[fd].curBlock = get_next_block(fcbArray[fd].curBlock);
      }
    fcbArray[fd].blockIndex += blocksRead;
    part2 = blocksRead * fs_vcb->block_size;
    }

  // LBAread remaining block into the fcb buffer, and reset buffer offset
  if (part3 > 0)
    {
    blocksRead = LBAread(fcbArray[fd].buf, 1, fcbArray[fd].curBlock);
    fcbArray[fd].bufLen = fs_vcb->block_size;

    fcbArray[fd].curBlock = get_next_block(fcbArray[fd].curBlock);
    fcbArray[fd].blockIndex += 1;
    fcbArray[fd].bufOff = 0;
    
    // if the number of bytes is more than zero, copy the fd buffer to the
    // buffer and set the offset to the position after the data amount.
    if (part3 > 0)
      {
      memcpy(buffer + part1 + part2, fcbArray[fd].buf + fcbArray[fd].bufOff, part3);
      fcbArray[fd].bufOff += part3;
      }
    }

  fcbArray[fd].fi->accessed = time(NULL);

  return part1 + part2 + part3;
	}

// interface to move files or directories
int b_move (char *dest, char *src)
  {
  
  // retrieve source path info
  DE *src_dir = parsePath(src);
	char *src_tok = get_last_tok(src);
	int src_index = get_de_index(src_tok, src_dir);

  if (src_index < 0)
    {
    perror("b_move: source file/directory not found");
    return -1;
    }

  // retrieve destination path info
  DE *dest_dir = parsePath(dest);
	char *dest_tok = get_last_tok(dest);
	int dest_index = get_de_index(dest_tok, dest_dir);

  if (dest_index > -1)
    {
    perror("b_move: file/directory with that name already exists");
    return -1;
    }

  // if the source and destination directories are the same, we just need to
  // copy the name of the destination directory to the source directory and
  // write the changes to disk
  if (dest_dir[0].loc == src_dir[0].loc)
    {
    strcpy(src_dir[src_index].name, dest_tok);
    write_dir(src_dir);

    free(src_dir);
    src_dir = NULL;
    free(src_tok);
    src_tok = NULL;

    free(dest_dir);
    dest_dir = NULL;
    free(dest_tok);
    dest_tok = NULL;

    return 0;
    }

  // retrieve an available directory entry from the directory
  dest_index = get_avail_de_idx(dest_dir);

  if (dest_index < 0)
    {
    return -1;
    }

  // copy the source directory entry to the destination directory entry
  memcpy(&dest_dir[dest_index], &src_dir[src_index], sizeof(DE));

  // replace the destination name with the new name, then write the changes to
  // disk
  strcpy(dest_dir[dest_index].name, dest_tok);
  write_dir(dest_dir);

  // reset the source directory entry to have no name and set the available flag
  src_dir[src_index].name[0] = '\0';
  src_dir[src_index].attr = 'a';
  write_dir(src_dir);

  free(src_dir);
  src_dir = NULL;
  free(src_tok);
  src_tok = NULL;

  free(dest_dir);
  dest_dir = NULL;
  free(dest_tok);
  dest_tok = NULL;

  return 0;
  }
	
// Interface to Close the file	
int b_close (b_io_fd fd)
	{
  // write any unsaved changes to the file buffer to disk
  if (!(fcbArray[fd].accessMode & O_RDONLY) && fcbArray[fd].bufOff > 0)
    LBAwrite(fcbArray[fd].buf, 1, fcbArray[fd].curBlock);

  // shrink the number of blocks the file occupies to optimize space
  int freespace_restored = restore_extra_free(fcbArray[fd].fi);
  
  // copy any changes to the fcb directory entry to the directory array
  // containing it.
  memcpy(&fcbArray[fd].dir_array[fcbArray[fd].fileIndex], fcbArray[fd].fi, sizeof(DE));

  write_all_fs(fcbArray[fd].dir_array);
  
  free(fcbArray[fd].dir_array);
  fcbArray[fd].dir_array = NULL;
  free(fcbArray[fd].fi);
  fcbArray[fd].fi = NULL;
  free(fcbArray[fd].buf);
  fcbArray[fd].buf = NULL;
	}

// print for debugging purposes
int print_fd(b_io_fd fd)
  {
  if (fd < 0)
    {
    return -1;
    }
  printf("=================== Printing FCB Entry ===================\n");
  printf("Index: %02d  Filename: %-10s  FileSize: %-8ld  NumBlocks: %d   bufOff: %04d   bufLen: %04d   curBlk: %#010x\n",
    fd, fcbArray[fd].fi->name, fcbArray[fd].fi->size, fcbArray[fd].fi->num_blocks, fcbArray[fd].bufOff, fcbArray[fd].bufLen, fcbArray[fd].curBlock * 4 + 0x0400);
  }