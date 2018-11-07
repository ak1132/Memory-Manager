/*
 * my_mem_manager.c
 *
 *  Created on: Oct 25, 2018
 *  Author: sg1425,ark159,vp406
 *  Machine: man.cs.rutgers.edu
 */
#include <sys/mman.h>
#include "my_pthread_t.h"
#include "my_mem_manager.h"

static char *memory;
inv_pte *invt_pg_table;
int *invt_swap_tb;
int mem_manager_init = 0;
FILE *swap_file;

float SHARED_SPACE = 0.25;
float KERNEL_SPACE = 0.25;
float PAGE_SPACE = 0.5;

int available_pages = 0;
int PAGE_SIZE = 0;
int TOTAL_PAGES = 0;
int SHARED_PAGES = 0;
int KERNEL_PAGES = 0;
int SWAP_PAGES = 0;

#undef malloc(x)
#undef free(x)

struct sigaction mem_access_sigact;

void setBit(int *A, int k) {
	A[k / 32] |= 1 << (k % 32);  // Set the bit at the k-th position in A[i]
}

int testBit(int *A, int k) {
	return ((A[k / 32] & (1 << (k % 32))) != 0);
}

void clearBit(int *A, int k) {
	A[k / 32] &= ~(1 << (k % 32));
}

void swap_pages(int pg1, int pg2) {

	// Verify if valid swap
	if (pg1 == pg2)
		return;

	// Update Thread Page Table Entries
	int i;
	pte *curr_pte;
	// Update mem_page_no of pg1 to pg2 inside Thread Page Table
	if (invt_pg_table[pg1].is_alloc == 1) {
		int pg1_tid = invt_pg_table[pg1].tid;

		curr_pte = thread_pt[pg1_tid];
		while (curr_pte != NULL && curr_pte->mem_page_no != pg1) {
			curr_pte = curr_pte->next;
		}

		if (curr_pte == NULL) {
			printf("Page not found in thread page table!!!\n");
			return;
		}

		curr_pte->mem_page_no = pg2;
	}
	// Update mem_page_no of pg2 to pg1 inside Thread Page Table
	if (invt_pg_table[pg2].is_alloc == 1 && pg2 != available_pages - 1) {
		int pg2_tid = invt_pg_table[pg2].tid;
		curr_pte = thread_pt[pg2_tid];
		while (curr_pte != NULL && curr_pte->mem_page_no != pg2) {
			curr_pte = curr_pte->next;
		}

		if (curr_pte == NULL) {
			printf("Page not found in thread page table!!!\n");
			return;
		}

		curr_pte->mem_page_no = pg1;
	}

	// Update Inverted Page Table
	inv_pte *p1 = &(invt_pg_table[pg1]);
	inv_pte *p2 = &(invt_pg_table[pg2]);
	void *holder = (void*) malloc(sizeof(inv_pte));

	memcpy(holder, p1, sizeof(inv_pte));
	memcpy(p1, p2, sizeof(inv_pte));
	memcpy(p2, holder, sizeof(inv_pte));

	void *mem_pg1 = memory + (pg1 * PAGE_SIZE);
	void *mem_pg2 = memory + (pg2 * PAGE_SIZE);

	//unprotect old page to swap out
	mprotect(mem_pg1, PAGE_SIZE, PROT_READ | PROT_WRITE);

	// Perform actual swap in Main memory
	void *temp = malloc(PAGE_SIZE);
	memcpy(temp, mem_pg1, PAGE_SIZE);
	memcpy(mem_pg1, mem_pg2, PAGE_SIZE);
	memcpy(mem_pg2, temp, PAGE_SIZE);
	free(temp);
}


void * special_alloc(size_t size, int type) {

	int alloc_complete = 0, i = 0, j = 0, old_pg = 0;

//	make_scheduler();
	init_mem_manager();

	void *ret_val;

	// get address of first shared page.
//	pte *curr_shared_pg = (void *) memory + TOTAL_PAGES * PAGE_SIZE;
	int shared_pg_index;
	int end_pg_index;
	if (type == SHARED_REGION) {
		shared_pg_index = TOTAL_PAGES + 1;
		end_pg_index = TOTAL_PAGES + SHARED_PAGES;
	} else if (type == KERNEL_REGION) {
		shared_pg_index = TOTAL_PAGES + SHARED_PAGES + 1;
		end_pg_index = TOTAL_PAGES + SHARED_PAGES + KERNEL_PAGES;
	}

	while (invt_pg_table[shared_pg_index].max_free < size
			&& shared_pg_index < end_pg_index) {
		shared_pg_index++;
	}

	if (invt_pg_table[shared_pg_index].max_free >= size) {
		// An existing page with free space more than 'size' is found.

		// Find a block in page more than or equal to 'size'
		int curr_offset = 0;
		pgm *itr_addr = (pgm*) (memory + (shared_pg_index * PAGE_SIZE));
		while (curr_offset < PAGE_SIZE
				&& !(itr_addr->free == 1
						&& (itr_addr->size > (sizeof(pgm) + size)))) {
			curr_offset += sizeof(pgm) + itr_addr->size;
			itr_addr = (void *) itr_addr + sizeof(pgm) + itr_addr->size;
		}

		split_pg_block(itr_addr, size);

		// Update max_block in the page
		if (itr_addr->is_max_free_block == 1) {
			itr_addr->is_max_free_block = 0;

			int curr_offset = 0;
			pgm *temp_addr = (pgm*) (memory + (shared_pg_index * PAGE_SIZE));
			invt_pg_table[shared_pg_index].max_free = 0;
			pgm *max_addr = NULL;

			while (curr_offset < PAGE_SIZE) {
				if (temp_addr->free == 1
						&& temp_addr->size
								> invt_pg_table[shared_pg_index].max_free) {
					invt_pg_table[shared_pg_index].max_free = temp_addr->size;
					max_addr = temp_addr;
				}

				curr_offset += sizeof(pgm) + temp_addr->size;
				temp_addr = (void *) temp_addr + sizeof(pgm) + temp_addr->size;
			}

			if (max_addr != NULL) {
				max_addr->is_max_free_block = 1;
			}
		}

		ret_val = (void *)itr_addr + sizeof(pgm);
//				alloc_complete = 1;

	} else {
		// The thread has used up all its virtual memory.
		if (type == KERNEL_REGION)
			printf("Kernel Region is full\n");
		else if (type == SHARED_REGION)
			printf("Shared Region is full\n");

		return NULL;
	}

	return ret_val;

}

int read_from_swap(int mem_index, int swap_index) {

	int swap_offset = swap_index * PAGE_SIZE;
	void *mem_addr = memory + mem_index * PAGE_SIZE;

	swap_file = fopen(SWAP_NAME, "r+");
	int x = lseek(fileno(swap_file), swap_offset, SEEK_SET);
	x = read(fileno(swap_file), mem_addr, PAGE_SIZE);
	close(fileno(swap_file));

	if (x < 1) {
		printf("Read form swap file failed!!!\n");
		return -1;
	}

	invt_pg_table[mem_index].is_alloc = 1;
	invt_pg_table[mem_index].tid = scheduler.running_thread->tid;
	invt_pg_table[mem_index].max_free = 0;

	clearBit(invt_swap_tb, swap_index);

	pgm *curr_pgm = memory + mem_index * PAGE_SIZE;
	while (curr_pgm != NULL) {
		if (curr_pgm->is_max_free_block == 1) {
			invt_pg_table[mem_index].max_free = curr_pgm->size;
			break;
		}
		curr_pgm = (void *) curr_pgm + sizeof(pgm) + curr_pgm->size;
	}

}

void mem_access_handler(int sig, siginfo_t *si, void *unused) {
//   printf("Got SIGSEGV at address: 0x%lx\n",(long) si->si_addr);
	int page_accessed = (int) (si->si_addr - (void *) memory) / PAGE_SIZE;
	void *accessed_page_addr = memory + (page_accessed * PAGE_SIZE);
	int i;

	if (scheduler.running_thread->tid == invt_pg_table[page_accessed].tid) {

		pte *curr_pte = thread_pt[scheduler.running_thread->tid];
		int pg_count = 0;
		while (curr_pte != NULL
				&& !(curr_pte->in_memory == 1
						&& curr_pte->mem_page_no == page_accessed)) {
			curr_pte = curr_pte->next;
			pg_count++;
		}

		if (page_accessed == pg_count) {
			// If the main memory has the correct page, then unprotect the page
			mprotect(accessed_page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);
			return;
		}
	}

	// The main memory access does not have the correct page.
	// Is the required present in Main Memory
	int actual_page = 0;
	pte *reqd_pte = thread_pt[scheduler.running_thread->tid];
	for (i = 0; i < page_accessed && reqd_pte != NULL; i++) {
		reqd_pte = reqd_pte->next;
	}

	if (reqd_pte == NULL) {
		printf("Page access out of bounds!!!\n");
		return;
	}

	if (reqd_pte->in_memory == 1) {
		actual_page = reqd_pte->mem_page_no;
		void *actual_page_addr = memory + (actual_page * PAGE_SIZE);

		//Unprotect the pages to be swapped
		mprotect(accessed_page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);
		mprotect(actual_page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);

		swap_pages(actual_page, page_accessed);

		// Re-Protect the swapped out page
		mprotect(actual_page_addr, PAGE_SIZE, PROT_NONE);

	} else {
		// Required page in Swap file

		// Take care of the page existing at 'page_accessed'

		int is_buffered = 0;
		int free_pg_index = find_free_page();
		if (free_pg_index == -1) {
			// swap from swap file
			free_pg_index = available_pages - 1;
			is_buffered = 1;
		}

		int old_tid = invt_pg_table[page_accessed].tid;

		void *page = memory + (page_accessed * PAGE_SIZE);
		mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);
		page = memory + (free_pg_index * PAGE_SIZE);
		mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);

		swap_pages(page_accessed, free_pg_index);

		page = memory + (free_pg_index * PAGE_SIZE);
		mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);

		int swap_file_index = reqd_pte->swap_page_no;

		read_from_swap(page_accessed, swap_file_index);

		reqd_pte->in_memory = 1;
		reqd_pte->mem_page_no = page_accessed;
		reqd_pte->swap_page_no = 0;

		if (is_buffered) {
			// Write Buffer to Swap File

			write_in_swap(free_pg_index, swap_file_index);
			pte *curr_pte = thread_pt[old_tid];
			while (curr_pte != NULL
					&& !(curr_pte->in_memory == 1
							&& curr_pte->mem_page_no == available_pages - 1)) {
				curr_pte = curr_pte->next;
			}

			if (curr_pte == NULL) {
				printf("Page not found\n");
			}

			curr_pte->in_memory = 0;
			curr_pte->swap_page_no = swap_file_index;
			curr_pte->mem_page_no = 0;
		}
	}
}

void switch_thread(int old_tid, int new_tid) {
	// Protect all pages
	mprotect(memory, PAGE_SIZE * TOTAL_PAGES, PROT_NONE);
}

void init_mem_manager() {
	if (mem_manager_init == 0) {

		PAGE_SIZE = sysconf(_SC_PAGE_SIZE);
		available_pages = MAIN_MEM_SIZE / PAGE_SIZE;
		TOTAL_PAGES = available_pages * PAGE_SPACE;
		SHARED_PAGES = available_pages * SHARED_SPACE;
		KERNEL_PAGES = available_pages * KERNEL_SPACE;
		KERNEL_PAGES--; // The last page in Main memory will be reserved for Buffer Space.
		SWAP_PAGES = (SWAP_SIZE / PAGE_SIZE);

		//	Initialize Inverted Page Table and mem-align the pages
		invt_pg_table = (inv_pte*) malloc(available_pages * sizeof(inv_pte));

		memory = (char*) memalign(PAGE_SIZE, MAIN_MEM_SIZE);

		int i = 0, j = 0;
		// Initialize Inverted Swap Table
		invt_swap_tb = (int *) malloc(SWAP_PAGES / 32);
		for (i = 0; i < SWAP_PAGES; i++) {
			clearBit(invt_swap_tb, i);
		}

		// Init Inverted Page Table
		for (i = 0; i < available_pages; i++) {

			invt_pg_table[i].tid = 0;
			invt_pg_table[i].is_alloc = 0;
			invt_pg_table[i].max_free = PAGE_SIZE - sizeof(pgm);
			pgm *pg_addr = (void *) (memory + (i * PAGE_SIZE));
			pg_addr->free = 1;
			pg_addr->size = PAGE_SIZE - sizeof(pgm);
			pg_addr->is_max_free_block = 1;
		}

		//Init Thread Page Table
		thread_pt = (pte **) malloc(sizeof(pte *));
		for (i = 0; i < MAX_THREADS; i++) {
			thread_pt[i] = NULL;
		}

		//init swap space
		swap_file = fopen(SWAP_NAME, "w+");

		ftruncate(fileno(swap_file), SWAP_SIZE);
		close(fileno(swap_file));
		mem_manager_init = 1;

		// Protect all pages
		mprotect(memory, PAGE_SIZE * TOTAL_PAGES, PROT_NONE);

		// Register Handler
		mem_access_sigact.sa_flags = SA_SIGINFO;
		sigemptyset(&mem_access_sigact.sa_mask);
		mem_access_sigact.sa_sigaction = mem_access_handler;

		if (sigaction(SIGSEGV, &mem_access_sigact, NULL) == -1) {
			printf("Fatal error setting up signal handler\n");
			exit(EXIT_FAILURE);    //explode!
		}
	}
}

void split_pg_block(pgm *itr_addr, int size) {

	pgm *new_ptr = (void *) itr_addr + sizeof(pgm) + size;
	new_ptr->free = 1;
	new_ptr->size = itr_addr->size - size - sizeof(pgm);

	itr_addr->free = 0;
	itr_addr->size = size;
}

int write_in_swap(int mem_index, int swap_index) {
	void *mem_page = (void *) memory + mem_index * PAGE_SIZE;
	int swap_offset = swap_index * PAGE_SIZE;

	// Unprotect page to be swapped out
	mprotect(mem_page, PAGE_SIZE, PROT_READ | PROT_WRITE);

	setBit(invt_swap_tb, swap_index);

	swap_file = fopen(SWAP_NAME, "r+");
	int x = lseek(fileno(swap_file), swap_offset, SEEK_SET);

	x = write(fileno(swap_file), mem_page, PAGE_SIZE);
	close(fileno(swap_file));
	return x;
}

/*
 *	Find a free page in main memory
 */
int find_free_page() {
	int pg_no;

	for (pg_no = 0; pg_no < TOTAL_PAGES && invt_pg_table[pg_no].is_alloc == 1;
			pg_no++)
		;

	if (pg_no == TOTAL_PAGES) {
		// No space left in main memory.
		return -1;
	} else {
		return pg_no;
	}
}

void *allocate_in_page(int tid, int pg_no, int size) {

	// Update Inverted Page Table
	inv_pte *free_pg_entry = &(invt_pg_table[pg_no]); 
	free_pg_entry->tid = tid;
	free_pg_entry->is_alloc = 1;
	free_pg_entry->max_free = free_pg_entry->max_free - sizeof(pgm) - size;

	pgm *free_page = (pgm*) (memory + (pg_no * PAGE_SIZE));
	split_pg_block(free_page, size);
	free_page->is_max_free_block = 0;
	((pgm *) ((void *) free_page + sizeof(pgm) + size))->is_max_free_block = 1;

	return (void *) free_page + sizeof(pgm);
}

void *init_pte(int pg_no) {

	// Create a new Page Table Entry for the page
	pte *new_pte = (pte *) malloc(sizeof(pte));
	new_pte->in_memory = 1;
	new_pte->dirty = 0;
	new_pte->mem_page_no = pg_no;
	new_pte->swap_page_no = 0;
	new_pte->next = NULL;

	return new_pte;
}

void * myallocate(size_t size, char *filename, int line_number, int flag) {

	int alloc_complete = 0, i = 0, j = 0, old_pg = 0;
	if (flag != THREADREQ) {
		return special_alloc(size, KERNEL_REGION);
//		return malloc(size);
	} else {

		make_scheduler();

		void *ret_val;
		int tid = scheduler.running_thread->tid;

		if (thread_pt[tid] == NULL) {
			// The threads is asking for the page for the first time.

			int vir_pg = 0;				// Since this is thread's first page.
			old_pg = find_free_page(tid, size);

			// Create a new Page Table Entry for the page
			pte *new_pte = init_pte(vir_pg);
			thread_pt[tid] = new_pte;

			if (old_pg == -1) {
				// No Space left in Main Memory
				printf("Main memory is full!!!\n");
				// TODO: Swap code
				int swap_index = 0;

				while (testBit(invt_swap_tb, swap_index)
						&& swap_index < SWAP_PAGES) {
					swap_index++;
				}

				if (swap_index == SWAP_PAGES) {
					printf("Swap is also FULL, You are Doomed!!!\n");
					return NULL;
				}

				int old_tid = invt_pg_table[0].tid;
				thread_pt[old_tid]->in_memory = 0;
				thread_pt[old_tid]->swap_page_no = swap_index;

				if (write_in_swap(thread_pt[old_tid]->mem_page_no, swap_index)
						== -1) {
					printf("Cant write in Swap File!!!\n");
					return NULL;
				}

				invt_pg_table[0].tid = tid;
				invt_pg_table[0].is_alloc = 1;
				invt_pg_table[0].max_free = PAGE_SIZE - sizeof(pgm);

				thread_pt[tid]->in_memory = 1;
				thread_pt[tid]->mem_page_no = 0;
				thread_pt[tid]->next = NULL;

				pgm * curr_pgm = memory;
				curr_pgm->free = 0;
				curr_pgm->is_max_free_block = 1;
				curr_pgm->size = PAGE_SIZE - sizeof(pgm);

				return allocate_in_page(tid, 0, size);

			}

			void *page = memory + (old_pg * PAGE_SIZE);
			mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);
			page = memory + (vir_pg * PAGE_SIZE);
			mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);

			swap_pages(old_pg, vir_pg);

			page = memory + (old_pg * PAGE_SIZE);

			// Unprotect newly assignned page
			mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);
			ret_val = allocate_in_page(tid, vir_pg, size);

		} else {

			int vir_pg = 0;
			pte *vir_pte = thread_pt[tid];

			for (vir_pg = 0; vir_pte->next != NULL; vir_pg++) {
				if (invt_pg_table[vir_pte->mem_page_no].max_free >= size) {
					break;
				}
				vir_pte = vir_pte->next;
			}

			if (invt_pg_table[vir_pte->mem_page_no].max_free >= size) {
				// An existing page with free space more than 'size' is found.

				int pg_no = vir_pte->mem_page_no;

				// Find a block in page more than or equal to 'size'
				int curr_offset = 0;
				pgm *itr_addr = (pgm*) (memory + (pg_no * PAGE_SIZE));
				while (curr_offset < PAGE_SIZE
						&& !(itr_addr->free == 1
								&& (itr_addr->size > (sizeof(pgm) + size)))) {
					curr_offset += sizeof(pgm) + itr_addr->size;
					itr_addr = (void *) itr_addr + sizeof(pgm) + itr_addr->size;
				}

				// Unprotect
				mprotect(itr_addr, PAGE_SIZE, PROT_READ | PROT_WRITE);

				split_pg_block(itr_addr, size);

				// Update max_block in the page
				if (itr_addr->is_max_free_block == 1) {
					itr_addr->is_max_free_block = 0;

					int curr_offset = 0;
					pgm *temp_addr = (pgm*) (memory + (pg_no * PAGE_SIZE));
					invt_pg_table[pg_no].max_free = 0;
					pgm *max_addr = NULL;

					while (curr_offset < PAGE_SIZE) {
						if (temp_addr->free == 1
								&& temp_addr->size
										> invt_pg_table[pg_no].max_free) {
							invt_pg_table[pg_no].max_free = temp_addr->size;
							max_addr = temp_addr;
						}

						curr_offset += sizeof(pgm) + temp_addr->size;
						temp_addr = (void *) temp_addr + sizeof(pgm)
								+ temp_addr->size;
					}

					if (max_addr != NULL) {
						max_addr->is_max_free_block = 1;
					}
				}

				ret_val = itr_addr + sizeof(pgm);

			} else if (vir_pg == TOTAL_PAGES - 1) {
				// The thread has used up all its virtual memory.
				printf("The thread %d has used up all his virtual memory!!!\n",
						tid);
				return NULL;
			} else {
				// The thread does not currently have a page large enough to hold the 'size' allocation.
				// Allocating a new page.

				// Id for new virtual page
				vir_pg++;

				// Create a new Page Table Entry for the page
				pte *new_pte = init_pte(vir_pg);
				vir_pte->next = new_pte;

				void *page = memory + (old_pg * PAGE_SIZE);
				mprotect(page, PAGE_SIZE, PROT_READ | PROT_WRITE);
				void *newpage = memory + (vir_pg * PAGE_SIZE);
				mprotect(newpage, PAGE_SIZE, PROT_READ | PROT_WRITE);

				old_pg = find_free_page(tid, size);
				if (old_pg == -1) {
					// No Space left in Main Memory
					printf("Main memory is full!!!\n");
					// TODO: Swap code
					int swap_index = 0;

					while (testBit(invt_swap_tb, swap_index)
							&& swap_index < SWAP_PAGES) {
						swap_index++;
					}

					if (swap_index == SWAP_PAGES) {
						printf("Swap is also FULL, You are Doomed!!!\n");
						return NULL;
					}

					int old_tid = invt_pg_table[vir_pg].tid;
					pte *curr_pte = thread_pt[old_tid];
					while (curr_pte != NULL
							&& !(curr_pte->in_memory == 1
									&& curr_pte->mem_page_no == vir_pg))
						curr_pte = curr_pte->next;

					curr_pte->in_memory = 0;
					curr_pte->swap_page_no = swap_index;

					if (write_in_swap(vir_pg, swap_index) == -1) {
						printf("Cant write in Swap File!!!\n");
						return NULL;
					}

					invt_pg_table[vir_pg].tid = tid;
					invt_pg_table[vir_pg].is_alloc = 1;
					invt_pg_table[vir_pg].max_free = PAGE_SIZE - sizeof(pgm);

					new_pte->in_memory = 1;
					new_pte->mem_page_no = vir_pg;
					new_pte->next = NULL;

					pgm * curr_pgm = memory + vir_pg * PAGE_SIZE;
					curr_pgm->free = 0;
					curr_pgm->is_max_free_block = 1;
					curr_pgm->size = PAGE_SIZE - sizeof(pgm);

					return allocate_in_page(tid, vir_pg, size);

				}

				swap_pages(old_pg, vir_pg);

				//reprotect old page
				page = memory + (old_pg * PAGE_SIZE);
				mprotect(page, PAGE_SIZE, PROT_NONE);

				void *new_page = memory + (vir_pg * PAGE_SIZE);
				mprotect(new_page, PAGE_SIZE, PROT_READ | PROT_WRITE);
				ret_val = allocate_in_page(tid, vir_pg, size);
			}

		}

		return ret_val;
	}
}

// TODO: dealloc_thread_mem()

void mydeallocate(void * ptr, char *filename, int line_number, int flag) {

	int alloc_complete = 0;
	make_scheduler();

	pgm *pg_meta = ptr - sizeof(pgm);
	pgm *curr_pgm;
	pg_meta->free = 1;

	int inv_pg_index = (int) ((void *) pg_meta - (void *) memory)
			/ (int) PAGE_SIZE;

	if (invt_pg_table[inv_pg_index].max_free < pg_meta->size) {
		invt_pg_table[inv_pg_index].max_free = pg_meta->size;
		pg_meta->is_max_free_block = 1;

		while (curr_pgm != NULL) {
			if (curr_pgm->is_max_free_block == 1) {
				curr_pgm->is_max_free_block = 0;
				break;
			}
			curr_pgm += sizeof(pgm) + curr_pgm->size;
		}
	}

	return;
}

void* shalloc(size_t size) {
	special_alloc(size, SHARED_REGION);
}
