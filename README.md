# Jumbo File System (JFS)

Jumbo File System (JFS) is a project to implement a simple direct indexed file system on top of a simulated disk. The file system provides functions similar to the Linux (POSIX) file system functions with some slight differences.

## Project Description

The objective of this project is to develop a simple direct indexed file system (JFS) with an emphasis on functionality similar to Linux (POSIX) file system functions. JFS allows direct reading or writing to any file without needing to open() or close() files beforehand. This project provides a simplified platform for demonstrating the working of file systems, inodes, and directory structures in a typical file system. 

## File Structure

The project contains the following main files:

- `command_line.c` : Contains main() function, and implements a simple prompt where users can type commands to interact with the file system.
  
- `jumbo_file_system.c` : The file system is implemented here.
  
- `basic_file_system.c` : This basic file system provides functions that allow allocating and releasing blocks on the disk.
  
- `raw_disk.c` : This disk simulation allows reading and writing specified blocks on the simulated disk, and uses a file on the real file system to store the simulated disk data.
