
#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#ifdef MM64
#include "mm64.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
static int time_slot;
static int num_cpus;
static int done = 0;
static struct krnl_t os;

#ifdef MM_PAGING
static unsigned long memramsz;
static unsigned long memswpsz[PAGING_MAX_MMSWP];

void free_pgtable_recursive(addr_t *table, int level) {
	if (!table || level < 1) return;

	// Nếu chưa tới cấp cuối (Page Table - PT), giải phóng các bảng con
	if (level > 1) {
		for (int i = 0; i < 512; i++) {
			if (table[i]) {
				free_pgtable_recursive((addr_t *)table[i], level - 1);
			}
		}
	}
	free(table);
}

void free_mm_struct(struct mm_struct *mm) {
	if (!mm) return;
	if (mm->pgd) {
		free_pgtable_recursive(mm->pgd, 5);
	}
	free(mm);
}

void free_krnl_struct(struct krnl_t *krnl) {
	if (!krnl) return;
	if (krnl->mm) {
		free_mm_struct(krnl->mm);
	}
#ifdef MM64
	if (krnl->krnl_pgd) free(krnl->krnl_pgd);
	if (krnl->krnl_p4d) free(krnl->krnl_p4d);
	if (krnl->krnl_pud) free(krnl->krnl_pud);
	if (krnl->krnl_pmd) free(krnl->krnl_pmd);
	if (krnl->krnl_pt) free(krnl->krnl_pt);
#else
	if (krnl->krnl_pgd) free(krnl->krnl_pgd);
#endif
	free(krnl);
}

void free_proc(struct pcb_t *proc) {
	if (!proc) return;
	if (proc->krnl) {
		free_krnl_struct(proc->krnl);
	}
	if (proc->code) {
		if (proc->code->text) free(proc->code->text);
		free(proc->code);
	}
	free(proc);
}

struct mmpaging_ld_args {
	/* A dispatched argument struct to compact many-fields passing to loader */
	int vmemsz;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	int active_mswp_id;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};


static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
		 	* ready queue */
			proc = get_proc();
			if (proc == NULL) {
                           next_slot(timer_id);
                           continue; /* First load failed. skip dummy load */
                        }
		}else if (proc->pc == proc->code->size) {
			/* The porcess has finish it job */
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			free_proc(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}

		pthread_mutex_lock(&done_mutex);
		int done_local = done;
		pthread_mutex_unlock(&done_mutex);
		/* Recheck process status after loading new process */
		if (proc == NULL && done_local) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		//proc->krnl->mm = proc->mm; //ADDED FOR
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void * ld_routine(void * args) {
#ifdef MM_PAGING
	struct memphy_struct* mram = ((struct mmpaging_ld_args *)args)->mram;
	struct memphy_struct** mswp = ((struct mmpaging_ld_args *)args)->mswp;
	struct memphy_struct* active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
	struct timer_id_t * timer_id = ((struct mmpaging_ld_args *)args)->timer_id;
#else
	struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif
	int i = 0;
  /* TODO init kernel page table directory */
#ifdef MM64
	os.krnl_pgd = (addr_t *)calloc(512, sizeof(addr_t));

	os.krnl_p4d = NULL;
	os.krnl_pud = NULL;
	os.krnl_pmd = NULL;
	os.krnl_pt  = NULL;

#else
	os.krnl_pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));
#endif
	i=0;
	printf("ld_routine\n");
	while (i < num_processes) {
		struct pcb_t * proc = load(ld_processes.path[i]);
		struct krnl_t * krnl = malloc(sizeof(struct krnl_t));
		if (krnl == NULL) {
			fprintf(stderr, "Failed to allocate kernel structure\n");
			exit(1);
		}
		*krnl = os;
		proc->krnl = krnl;

		krnl->mram = os.mram;
		krnl->mswp = os.mswp;
		krnl->active_mswp = os.active_mswp;
		krnl->active_mswp_id = os.active_mswp_id;

#ifdef MM64
		krnl->krnl_pgd = (addr_t *)calloc(512, sizeof(addr_t));
		krnl->krnl_p4d = NULL;
		krnl->krnl_pud = NULL;
		krnl->krnl_pmd = NULL;
		krnl->krnl_pt = NULL;
#else
		krnl->krnl_pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));
#endif

#ifdef MLQ_SCHED
		proc->prio = ld_processes.prio[i];
#endif
		while (current_time() < ld_processes.start_time[i]) {
			next_slot(timer_id);
		}
#ifdef MM_PAGING
		//proc->mm = malloc(sizeof(struct mm_struct));
		//init_mm(proc->mm, proc);
		//krnl->mm = proc->mm;
		krnl->mm = malloc(sizeof(struct mm_struct));
		if (krnl->mm == NULL) {
			fprintf(stderr, "Failed to allocate mm_struct\n");
			exit(1);
		}
		init_mm(krnl->mm, proc);
		krnl->mram = mram;
		krnl->mswp = mswp;
		krnl->active_mswp = active_mswp;
		krnl->active_mswp_id = ((struct mmpaging_ld_args *)args)->active_mswp_id;
#endif
		printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
			ld_processes.path[i], proc->pid, ld_processes.prio[i]);
		add_proc(proc);
		free(ld_processes.path[i]);
		i++;
		next_slot(timer_id);
	}
	free(ld_processes.path);
	free(ld_processes.start_time);
#ifdef MLQ_SCHED
	free(ld_processes.prio);
#endif
	pthread_mutex_lock(&done_mutex);
	done = 1;
	pthread_mutex_unlock(&done_mutex);
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void read_config(const char * path) {
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}
	fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	if (!ld_processes.path) {
		fprintf(stderr, "Failed to allocate process paths\n");
		fclose(file);
		exit(1);
	}

	ld_processes.start_time = (unsigned long*)malloc(sizeof(unsigned long) * num_processes);
	if (!ld_processes.start_time) {
		fprintf(stderr, "Failed to allocate start times\n");
		free(ld_processes.path);
		fclose(file);
		exit(1);
	}
		//malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING
	int sit;
	char first_line[256];
	int has_pending_process_line = 0;
#ifdef MM_FIXED_MEMSZ
	/* We provide here a back compatible with legacy OS simulatiom config file
         * In which, it have no addition config line for Mema, keep only one line
	 * for legacy info 
         *  [time slice] [N = Number of CPU] [M = Number of Processes to be run]
         */
        memramsz  =  0x100000000;
        memswpsz[0] = 0x1000000;
	for(sit = 1; sit < PAGING_MAX_MMSWP; sit++)
		memswpsz[sit] = 0;
#else
	/* Read input config of memory size: MEMRAM and upto 4 MEMSWP (mem swap)
	 * Format: (size=0 result non-used memswap, must have RAM and at least 1 SWAP)
	 *        MEM_RAM_SZ MEM_SWP0_SZ MEM_SWP1_SZ MEM_SWP2_SZ MEM_SWP3_SZ
	*/
	//fscanf(file, FORMAT_ARG "\n", &memramsz);
	//for(sit = 0; sit < PAGING_MAX_MMSWP; sit++)
	//	fscanf(file, FORMAT_ARG, &(memswpsz[sit]));

    //   fscanf(file, "\n"); /* Final character */
#endif
	unsigned long tmp_ram, tmp_swp0, tmp_swp1, tmp_swp2, tmp_swp3;
	char extra;

	if (fgets(first_line, sizeof(first_line), file) != NULL) {
		int matched = sscanf(
			first_line,
			"%lu %lu %lu %lu %lu %c",
			&tmp_ram,
			&tmp_swp0,
			&tmp_swp1,
			&tmp_swp2,
			&tmp_swp3,
			&extra
		);

		if (matched == 5) {
#ifndef MM_FIXED_MEMSZ
			memramsz = tmp_ram;
			memswpsz[0] = tmp_swp0;
			memswpsz[1] = tmp_swp1;
			memswpsz[2] = tmp_swp2;
			memswpsz[3] = tmp_swp3;
#endif
			// Nếu MM_FIXED_MEMSZ bật thì bỏ qua dòng memory trong input.
		} else {
			has_pending_process_line = 1;
		}
	}
#endif

#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#endif
	int i;
	for (i = 0; i < num_processes; i++) {
		ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		char proc[100];
		char line[256];
#ifdef MM_PAGING
		if (i == 0 && has_pending_process_line) {
			strcpy(line, first_line);
		} else
#endif
		{
			if (fgets(line, sizeof(line), file) == NULL) {
				fprintf(stderr, "Missing process config line at index %d\n", i);
				fclose(file);
				exit(1);
			}
		}
#ifdef MLQ_SCHED
		if (sscanf(line, "%lu %99s %lu",
				   &ld_processes.start_time[i],
				   proc,
				   &ld_processes.prio[i]) != 3) {
			fprintf(stderr, "Invalid process config line: %s\n", line);
			fclose(file);
			exit(1);
				   }
#else
		if (sscanf(line, "%lu %99s",
				   &ld_processes.start_time[i],
				   proc) != 2) {
			fprintf(stderr, "Invalid process config line: %s\n", line);
			fclose(file);
			exit(1);
				   }
#endif

		if (strlen("input/proc/") + strlen(proc) >= 100) {
			fprintf(stderr, "Process path too long: %s\n", proc);
			fclose(file);
			exit(1);
		}

		strcat(ld_processes.path[i], proc);
	}
	fclose(file);
}

int main(int argc, char * argv[]) {
	/* Read config */
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}
	char path[256];
	if (strlen(argv[1]) > 240) {
		fprintf(stderr, "Config file path too long\n");
		return 1;
	}
	snprintf(path, sizeof(path), "input/%s", argv[1]);
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	if (!cpu) {
		fprintf(stderr, "Failed to allocate CPU threads\n");
		return 1;
	}
	struct cpu_args * args =
		(struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;
	
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
	/* Init all MEMPHY include 1 MEMRAM and n of MEMSWP */
	int rdmflag = 1; /* By default memphy is RANDOM ACCESS MEMORY */

	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];

	/* Create MEM RAM */
	init_memphy(&mram, memramsz, rdmflag);

        /* Create all MEM SWAP */ 
	int sit;
	for(sit = 0; sit < PAGING_MAX_MMSWP; sit++)
	       init_memphy(&mswp[sit], memswpsz[sit], rdmflag);
	struct memphy_struct *mswp_ptr[PAGING_MAX_MMSWP];
	for(sit = 0; sit < PAGING_MAX_MMSWP; sit++) {
		mswp_ptr[sit] = &mswp[sit];
	}

	/* In Paging mode, it needs passing the system mem to each PCB through loader*/
	struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));

	mm_ld_args->timer_id = ld_event;
	mm_ld_args->mram = (struct memphy_struct *) &mram;
	mm_ld_args->mswp = mswp_ptr;
	mm_ld_args->active_mswp = (struct memphy_struct *) &mswp[0];
        mm_ld_args->active_mswp_id = 0;


#endif

	/* Init scheduler */
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif
	for (i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL,
			cpu_routine, (void*)&args[i]);
	}

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	/* Stop timer */
	stop_timer();

	free(cpu);
	free(args);

#ifdef MM_PAGING
	free(mm_ld_args);
#endif
#ifdef MM64
	if (os.krnl_pgd) {
		free_pgtable_recursive(os.krnl_pgd, 5);
	}
#else
	if (os.krnl_pgd) {
		free_pgtable_recursive(os.krnl_pgd, 5);
	}
#endif
	pthread_mutex_destroy(&done_mutex);
	printf("\n[OS] Simulation finished symbols clean up.\n");
	return 0;

}



