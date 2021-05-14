# Virtual-Memory-Manager

C++ implementation/simulation of an Operating System's Virtual Memory Manager operations, which maps the virtual address spaces of multiple processes onto physical frames using page table translation. Given a set of instructions I simulate the behavior of the hardware. 

Assumes multiple processes, each with its own virtual address space of exactly 64 virtual pages (not realistic, but valid in principle). Paging is also implemented. Supports up to 128 frames.
