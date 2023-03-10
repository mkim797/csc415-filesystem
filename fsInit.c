/**************************************************************
* Class:  CSC-415-02 Fall 2022
* Names: Mark Kim, Peter Truong, Chengkai Yang, Zeel Diyora
* Student IDs: 918204214, 915780793, 921572896, 920838201
* GitHub Name: mkim797
* Group Name: Diligence
* Project: Basic File System
*
* File: fsInit.c
*
* Description: Main driver for file system assignment.
*
* This file is where you will start and initialize your system
*
**************************************************************/


#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>

#include "fsLow.h"
#include "mfs.h"
#include "fsFree.h"
#include "fsDir.h"
#include "fsHelpers.h"

VCB *fs_vcb;
int *freespace;
char *cw_path;
DE *cw_dir_array;

int initFileSystem (uint64_t numberOfBlocks, uint64_t blockSize)
	{
	printf ("Initializing File System with %ld blocks with a block size of %ld\n", numberOfBlocks, blockSize);
	/* TODO: Add any code you need to initialize your file system. */

	// malloc space for the VCB that will take up an entire block
	fs_vcb = malloc(blockSize);

	// malloc freespace by the number of blocks requested multiplied by the size
	// of an integer.
	freespace = malloc(get_num_blocks(sizeof(int) * numberOfBlocks,
    blockSize)*blockSize);

	// read in the volume control block from the first block of file system
	LBAread(fs_vcb, 1, 0);

	// check if the volume control block is the one we are looking for
	if (fs_vcb->magic == 678267415)
		{
		if (load_free() != fs_vcb->freespace_size)
			{
			perror("Freespace load failed\n");
			return -1;
			};
		}
	else
		{
		fs_vcb->magic = 678267415;
		fs_vcb->number_of_blocks = numberOfBlocks;
		fs_vcb->block_size = blockSize;

		// init_free initializes freespace_first and freespace_avail of the VCB
		fs_vcb->freespace_loc = init_free();
		// print_free(fs_vcb, freespace);
		fs_vcb->root_loc = init_dir(0);

		if (LBAwrite(fs_vcb, 1, 0) != 1)
			{
			perror("LBAwrite failed when trying to write the VCB\n");
			return EXIT_FAILURE;
			}		
		}

	// malloc then copy newly created root directory to current working
	// directory array for ease of tracking.
	cw_dir_array = malloc(fs_vcb->block_size * fs_vcb->root_blocks);
	LBAread(cw_dir_array, fs_vcb->root_blocks, fs_vcb->root_loc);

	// malloc then set the path to root.
	cw_path = malloc(MAX_PATH_LENGTH);
	strcpy(cw_path, "/");
		
	return 0;
	}


void exitFileSystem ()
	{
	printf ("System exiting\n");
	if (LBAwrite(fs_vcb, 1, 0) != 1)
		{
		perror("LBAwrite failed when trying to write the VCB\n");
		}
	
	if (LBAwrite(freespace, fs_vcb->freespace_size, 1) != fs_vcb->freespace_size)
		{
		perror("LBAwrite failed when trying to write the freespace\n");
		}
	
	free(freespace);
	freespace = NULL;
	free(fs_vcb);
	fs_vcb = NULL;
	free(cw_dir_array);
	cw_dir_array = NULL;
	free(cw_path);
	cw_path = NULL;
	}