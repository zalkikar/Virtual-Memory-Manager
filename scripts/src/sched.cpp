#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <deque>
#include <cassert>

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <ctype.h>
#include <unistd.h>



// structs

struct INSTRUCTION {
	char type;
	int a;
};

struct VMA {
	// Each VMA is comprised of these 4 numbers.
    unsigned int start_vpage:7;
    unsigned int end_vpage:7;
    unsigned int write_protected:1;
    unsigned int file_mapped:1;
};

struct pte_t {
	// Bit Fields
	// PRESENT/VALID, REFERENCED, MODIFIED, WRITE_PROTECT, and PAGEDOUT
	// and the number of the physical frame (in case the pte is present).
	// Assuming that the maximum number of frames is 128, which equals 7 bits and the mentioned 5 bits above
	unsigned int PRESENT:1;
	unsigned int REFERENCED:1;
	unsigned int MODIFIED:1;
	unsigned int WRITE_PROTECT:1;
	unsigned int PAGEDOUT:1;
	unsigned int FRAME_NUMBER:7;
	// 20 bits remain, using only 2
	unsigned int FILE_MAPPED:1;
	unsigned int VALID_ADDR:1;
};

struct frame_t {
	int pid;
	int FRAME_NUMBER;
	int PAGE;
	int VICT;
    unsigned long tau;
	unsigned long age;
};

struct process_stats {
	unsigned long fouts;
	unsigned long outs;
	unsigned long fins;
	unsigned long ins;
	unsigned long zeros;
	unsigned long segv;
	unsigned long segprot;
	unsigned long unmaps;
	unsigned long maps;
};


// process class

class Process {
	public:
		int pid;
		std::vector<VMA> VMAs;
		std::vector<pte_t> page_table;
		process_stats pstats;
};


// constants and non-constant vars
unsigned long long cost = 0; // total cost, 64-bit counter for total cost calculation overrunning 2^32
const int pte_count = 64; // 64 page table entries (pte_t)
int frame_count = 128; // 128 is max frame count
int instruction_count = 0;  //instruction counter
std::vector<INSTRUCTION> instructions; // vector of instructions
std::vector<Process> processes; // vector of processes
std::deque<frame_t> frame_table; // global frame table
std::deque<frame_t*> frames_free; // free frames deque for allocations
std::deque<int> rand_values; // rfile vlaues


// general Pager class w/ virtual function 
class Pager {
	public:
		virtual frame_t* select_victim_frame() = 0; // virtual base class
};

// fifo
class FIFO : public Pager {
	public:
		FIFO();
		int idx;
		int ft_size;
		frame_t *ft;
		frame_t *ft2;
		frame_t *select_victim_frame();
};
FIFO::FIFO() {
	this->ft_size = frame_table.size();
	this->ft = &frame_table.front();
	this->idx = 0;
}
frame_t *FIFO::select_victim_frame() {
	idx++;
	ft2 = ft;
	ft2->VICT = 1;
	if (idx == ft_size) { idx = 0; }
	ft = &frame_table[idx];
	return ft2;
}

// random
class RANDOM : public Pager {
	public:
		RANDOM(int rand_cnt);
		int roll;
		int rand_cnt;
		frame_t *ft;
		frame_t *select_victim_frame();
};
RANDOM::RANDOM(int rand_cnt) { 
	this->rand_cnt = rand_cnt; 
	this->roll = 0;
}
frame_t *RANDOM::select_victim_frame() {
	if (roll == rand_cnt) { roll = 0; }
	frame_table[rand_values[roll]%frame_count].VICT = 1;
	ft = &frame_table[rand_values[roll]%frame_count];
	roll++;
	return ft;
}

// clock
class CLOCK : public Pager {
	public:
		CLOCK();
		int idx;
		int ft_size;
		pte_t *p;
		frame_t *ft;
		frame_t *ft2;
		frame_t *select_victim_frame();
};
CLOCK::CLOCK() {
	this->ft_size = frame_table.size();
	this->ft = &frame_table[0];
	this->idx = 0;
	this->p = &processes[this->ft->pid].page_table[this->ft->PAGE];
}
frame_t *CLOCK::select_victim_frame() {
	p = &processes[ft->pid].page_table[ft->PAGE];
	while (p->REFERENCED == 1) {
		p->REFERENCED = 0;
		idx++;
		if (idx == ft_size) { idx = 0; }
		ft = &frame_table[idx];
		p=&processes[ft->pid].page_table[ft->PAGE];
	}
	idx++;
	if (idx == ft_size) { idx = 0; }
	ft=&frame_table[idx];
	ft2=&frame_table[p->FRAME_NUMBER];
	ft2->VICT = 1;
	return ft2;
}

// nru
class NRU : public Pager {
	public:
		NRU();
		int idx;
		int ft_size;
		int p;
		int v;
		int clas[4];
		pte_t *pte;
		frame_t *ft;
		frame_t *ft2;
		frame_t *select_victim_frame();
};
NRU::NRU() {
	this->p = 0;
	this->idx = 0;
	this->v = 0;
	this->ft_size = frame_table.size();
	for (int i=0; i<4; i++) { clas[i] = -1; }
}
frame_t *NRU::select_victim_frame() {
	v=0;
	ft_size=frame_table.size();
	for (int i=0; i<4; i++) { clas[i] = -1; }
	for (int j=0; j<ft_size; j++) {
		ft2=&frame_table[(p+j)%ft_size];
		pte=&processes[ft2->pid].page_table[ft2->PAGE];
		if (clas[2*pte->REFERENCED+pte->MODIFIED]==-1) {
			if (2*pte->REFERENCED+pte->MODIFIED==0) { p=((p+j)%ft_size+1)%ft_size; ft=ft2; ft->VICT = 1; v=1; break;
			} else { clas[2*pte->REFERENCED+pte->MODIFIED]=ft2->FRAME_NUMBER; }
		}
	}
	if (v==0) {
		for (int i=0; i<4; i++) {
			if (clas[i] != -1) { ft = &frame_table[clas[i]]; p=(ft->FRAME_NUMBER+1)%ft_size; ft->VICT = 1; v=1; break; }
		}
	}
	if (50<=instruction_count-idx) {
		for (int i=0; i<ft_size; i++) {
			ft2=&frame_table[i];
			if (ft2->pid != -1) { processes[ft2->pid].page_table[ft2->PAGE].REFERENCED = 0; }
		}
		idx=instruction_count;
	}
	return ft;
}

// aging
class AGING : public Pager {
	public:
		AGING();
		int p;
		int ft_size;
		frame_t *ft;
		frame_t *ft2;
		frame_t *select_victim_frame();
};
AGING::AGING() { 
	this->p= 0; 
	this->ft_size=frame_table.size();
	this->ft=&frame_table[this->p];
}
frame_t *AGING::select_victim_frame() {
	ft=&frame_table[p];
	for (int i=0; i<ft_size; i++) {
		ft2=&frame_table[(i+p)%ft_size];
		ft2->age = ft2->age >> 1;
		if (processes[ft2->pid].page_table[ft2->PAGE].REFERENCED == 1) {
			ft2->age = (ft2->age | 0x80000000);
			processes[ft2->pid].page_table[ft2->PAGE].REFERENCED = 0;
		}
		if (ft2->age<ft->age) { ft=ft2; }
	}
	p=(1+ft->FRAME_NUMBER)%ft_size;
	ft->VICT=1;
	return ft;
}


// working set
class WORKING_SET : public Pager {
	public:
		WORKING_SET();
		int h;
		int a;
		int m;
		int ft_size;
		pte_t *pte;
		frame_t *ft;
		frame_t *ft2;
		frame_t *select_victim_frame();
};
WORKING_SET::WORKING_SET() {
	this->h=0;
	this->a=0;
	this->m=-1000;
	this->ft_size=frame_table.size();
	this->ft2=NULL;
}
frame_t *WORKING_SET::select_victim_frame() {
	ft2=NULL;
	m=-1000;
	for (int i=0; i<ft_size; i++) {
		ft=&frame_table[(i+h)%ft_size];
		pte=&processes[ft->pid].page_table[ft->PAGE];
		a=instruction_count-1-ft->tau;
		if (pte->REFERENCED) {
			pte->REFERENCED=0;
			ft->tau=instruction_count-1; 
		} else if (50<=a) {
			ft2=ft;
			ft2->VICT=1;
			h=(ft2->FRAME_NUMBER+1)%ft_size;
			return ft2;
		} else if (m<a) { m=a; ft2=ft; }
	}
	if(ft2 == NULL) { ft2=&frame_table[h]; }
	h=(ft2->FRAME_NUMBER+1)%ft_size;
	ft2->VICT=1;
	return ft2;
}



void __costadd(int actioncode) {
	const int costamount[] = { 2400, 2700, 2800, 3100, 140, 340, 420, 400, 300 };
	cost += costamount[actioncode];
}

void __actionout(int actioncode, int procID = -1, int page = -1) {
    const char* actionstr[] = { " FOUT", " OUT", " FIN", " IN", " ZERO", " SEGV", " SEGPROT", " UNMAP ", " MAP " };
	if (actioncode == 7) { assert (procID!=-1); assert (page!=-1); std::cout << actionstr[actioncode] << procID << ":" << page << "\n";
	} else if (actioncode == 8) { assert (page!=-1); std::cout << actionstr[actioncode] << page << "\n";
	} else { std::cout << actionstr[actioncode] << "\n"; }
}


Pager *pager;
int main (int argc, char* argv[]) {
	
	// optional read/parse of arguments
    int c;
    char algo [1];
    bool O,F,P,S = false;
	bool xopt,yopt,fopt,aopt = false;
    while ((c = getopt (argc, argv, "f:a:o:")) != -1){
        switch (c) {
            case 'f': sscanf(optarg,"%d",&frame_count); break;
            case 'a': sscanf(optarg,"%c",algo); break;
            case 'o': 
                for(int i=0; optarg[i]!='\0'; i++) {
                        if(optarg[i] == 'O') { O = true; }
                        else if(optarg[i] == 'F') { F = true; }
                        else if(optarg[i] == 'P') { P = true; }
                        else if(optarg[i] == 'S') { S = true; }
						else if(optarg[i] == 'x') { xopt = true; }
						else if(optarg[i] == 'y') { yopt = true; }
						else if(optarg[i] == 'f') { fopt = true; }
						else if(optarg[i] == 'a') { aopt = true; }
				} 
				break;
            default:
                abort();
        }
    }


    // Prase input with ifstream
	// ALL lines starting with ‘#’ must be ignored and are provided simply for documentation and readability.
    int type, process_cnt, vma_cnt;
	std::ifstream inputfile (argv[argc -2]); // inputs
	std::string line;
	while(getline(inputfile, line)) {
		if (line[0] == '#') { continue; 
		} else{ process_cnt = atoi(&line[0]); getline(inputfile, line); break; }
	}
	for (int i = 0; i < process_cnt; i++) {
		Process p;
		p.pid = i;
		p.pstats.unmaps = p.pstats.maps = p.pstats.ins = p.pstats.outs = p.pstats.fins = p.pstats.fouts = p.pstats.zeros = p.pstats.segv = p.pstats.segprot = 0;
		for (int i = 0; i < pte_count; i++) {
			// the pte (i.e. all bits) should be initialized to “0” before the instruction simulation starts.
			pte_t pte;
			pte.FRAME_NUMBER = pte.PRESENT = pte.WRITE_PROTECT = pte.MODIFIED = pte.REFERENCED = pte.PAGEDOUT = pte.FILE_MAPPED = pte.VALID_ADDR = 0;
			p.page_table.push_back(pte);
		}
		while (line[0] == '#') { getline(inputfile, line); } 
		vma_cnt = atoi(&line[0]);
		getline(inputfile, line);
		for (int j = 0; j < vma_cnt; j++) {
			std::vector<std::string> tokens;
			std::stringstream ss(line);
			std::string token;
			while (getline(ss, token, ' ')) { tokens.push_back(token); }
			if (tokens.size() == 4) {
				VMA vma;
				vma.start_vpage = atoi(tokens[0].c_str());
				vma.end_vpage = atoi(tokens[1].c_str());
				vma.write_protected = atoi(tokens[2].c_str());
				vma.file_mapped = atoi(tokens[3].c_str());
				p.VMAs.push_back(vma);
			}
			getline(inputfile, line);
		}
		processes.push_back(p);
	}
	// instructions
	while (getline(inputfile, line)) {
		if (line[0] == '#') { continue; }
		std::vector<std::string> tokens;
		std::stringstream ss(line);
		std::string token;
		while (getline(ss, token, ' ')){ tokens.push_back(token); }
		if (tokens.size() == 2) {
			INSTRUCTION instruction;
			instruction.type = *tokens[0].c_str();
			instruction.a = atoi(tokens[1].c_str());
			instructions.push_back(instruction);
		}
	}
	inputfile.close();


	//random file
	std::ifstream inputfile2(argv[argc -1]);
	getline(inputfile2, line); 
	int rand_cnt = atoi(&line[0]);
	while (getline(inputfile2, line)) { rand_values.push_back(atoi(&line[0])); }
	inputfile2.close();


	// fte initialize
	for (int i = 0; i < frame_count; i++) {
        frame_t fte;
        fte.FRAME_NUMBER = i;
        fte.pid = fte.PAGE = -1;
        fte.VICT = fte.age = fte.tau = 0;
        frame_table.push_back(fte);
    }
    for (int i = 0; i < frame_count; i++) {
        frame_t *fte;
        fte = &frame_table[i];
        frames_free.push_back(fte);
    }


	// algo initialize
	if (*algo == 'f') { pager = new FIFO();
    } else if (*algo == 'r') { pager = new RANDOM(rand_cnt);
	} else if (*algo == 'c') { pager = new CLOCK();
	} else if (*algo == 'e') { pager = new NRU();
	} else if (*algo == 'a') { pager = new AGING();
	} else if (*algo == 'w') { pager = new WORKING_SET();
	} else { exit(0); }




	/*	*****	SIMULATION	*****  */

	// counters and pointers initialization
	unsigned long counter = 0;
	unsigned long long ctx_switches, process_exits = 0;
	pte_t *pte;
	frame_t *new_frame;
	Process *current_process;
	
	while ( counter < instructions.size() ) {
		instruction_count++;
		if ( O ) { printf("%lu: ==> %c %d\n", counter, instructions[counter].type, instructions[counter].a); }
		if (instructions[counter].type == 'c') {
			current_process = &processes[instructions[counter].a];
			cost+=130;
			ctx_switches++;
			counter++;
		} else if (instructions[counter].type == 'e') {
            std::cout << "EXIT current process " << instructions[counter].a << "\n";
			process_exits++;
			cost+=1250;
            for (int i = 0; i < processes[instructions[counter].a].page_table.size(); i++){
                pte = &processes[instructions[counter].a].page_table[i];
				pte->PAGEDOUT = 0;
                if (pte->PRESENT) {
                    new_frame = &frame_table[pte->FRAME_NUMBER];
					__costadd(7);
					if ( O ) { __actionout(7,new_frame->pid,new_frame->PAGE); }
					processes[instructions[counter].a].pstats.unmaps++;
					new_frame->VICT = 0;
                    frames_free.push_back(new_frame);
                    if ((pte->MODIFIED) && (pte->FILE_MAPPED)) {
						processes[instructions[counter].a].pstats.fouts++;
						__costadd(0);
						if ( O ) {  __actionout(0); }
                    }
                }
                pte->PRESENT = 0;
            }
			counter++;
        } else if (instructions[counter].type == 'r' || instructions[counter].type == 'w') {
			cost+=1;
			pte=&current_process->page_table[instructions[counter].a];

			// accessing a page (“r” or “w”) and the page is not present
			if (!pte->PRESENT) {
				if (pte->VALID_ADDR != 1) { // little faster than traversing through VMAs everytime
					for (int x = 0; x < current_process->VMAs.size(); x++) {
						if ((current_process->VMAs[x].start_vpage <= instructions[counter].a) && (current_process->VMAs[x].end_vpage >= instructions[counter].a)) {
							// set pte_t bits on the first page fault to that virtual page.
							pte->VALID_ADDR = 1;
							pte->FILE_MAPPED = current_process->VMAs[x].file_mapped;
							pte->WRITE_PROTECT = current_process->VMAs[x].write_protected;
							break;
						}
					}
					if (pte->VALID_ADDR != 1) {
						__costadd(5);
						if ( O ) { __actionout(5); } 
						current_process->pstats.segv++;
						counter++;
						continue;
					}
				}

				// allocate frame, either from free frames or from paging
				if (frames_free.size() == 0) { 
					new_frame = pager->select_victim_frame(); // victim frame
					pte_t *p = &processes[new_frame->pid].page_table[new_frame->PAGE];
					p->PRESENT = 0;
					processes[new_frame->pid].pstats.unmaps++;
					__costadd(7);
					if ( O ) { __actionout(7, new_frame->pid,new_frame->PAGE); } 
					if (p->MODIFIED) {
						p->MODIFIED = false;
						if (p->FILE_MAPPED) {
							__costadd(0);
							if ( O ) { __actionout(0); }
							processes[new_frame->pid].pstats.fouts++;
						} else {
							p->PAGEDOUT = true;
							__costadd(1);
							if ( O ) { __actionout(1); }
							processes[new_frame->pid].pstats.outs++;
						}
					}
				} else { new_frame = frames_free.front(); frames_free.pop_front(); }

				if (pte->FILE_MAPPED) {
					__costadd(2);
					if ( O ) { __actionout(2); }
					current_process->pstats.fins++;
				} else if (pte->PAGEDOUT) {
						__costadd(3);
					if ( O ) { __actionout(3); }
					current_process->pstats.ins++;
				} else {
					__costadd(4);
					if ( O ) { __actionout(4); }
					current_process->pstats.zeros++;
				}

				__costadd(8);
				if ( O ) { __actionout(8,-1,new_frame->FRAME_NUMBER); } 
				current_process->pstats.maps++;
				pte->PRESENT = 1;
				pte->FRAME_NUMBER = new_frame->FRAME_NUMBER;
				new_frame->age = 0;
				new_frame->pid = current_process->pid;
				new_frame->PAGE = instructions[counter].a;
				new_frame->tau = instruction_count-1;
			}
			pte->REFERENCED = 1; 

			if (instructions[counter].type == 'w') {
				if (pte->WRITE_PROTECT) {
					__costadd(6);
					if ( O ) { __actionout(6); }
					current_process->pstats.segprot++;
				} else { pte->MODIFIED = 1; }
			}
			counter++;
		}
	}

	// outputs
	if ( P ) {
        for (int i=0; i<processes.size(); i++) {
			std::cout << "PT[" << i << "]: ";
			for (int j=0; j<pte_count; j++) {
				if (processes[i].page_table[j].PRESENT == 1) {
					std::cout << j << ":";
					if (processes[i].page_table[j].REFERENCED == 1) { std::cout << "R";
					} else { std::cout << "-"; }
					if (processes[i].page_table[j].MODIFIED == 1) { std::cout << "M";
					} else { std::cout << "-"; }
					if (processes[i].page_table[j].PAGEDOUT == 1) { std::cout << "S ";
					} else { std::cout << "- "; }
				} else {
					if (processes[i].page_table[j].PAGEDOUT == 1) { std::cout << "# ";
					} else {std::cout << "* "; }
				}
			}
			std::cout << "\n";
		}
	}
    if ( F ) {
		std::cout << "FT: ";
		for (int i=0; i<frame_table.size(); i++) {
			if (frame_table[i].pid != -1) { std::cout << frame_table[i].pid << ":" << frame_table[i].PAGE << " ";
			} else { std::cout << "* "; }
		}
		std::cout << "\n";
	}
    if ( S ) {
        for (int i=0; i<processes.size(); i++) {
			printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
							processes[i].pid,
							processes[i].pstats.unmaps, processes[i].pstats.maps, processes[i].pstats.ins, processes[i].pstats.outs,
							processes[i].pstats.fins, processes[i].pstats.fouts, processes[i].pstats.zeros,
							processes[i].pstats.segv, processes[i].pstats.segprot);
		}
        printf("TOTALCOST %lu %llu %llu %llu %lu\n", 
				instructions.size(), ctx_switches, process_exits, cost, sizeof(pte_t));
    }

	return 1;
}