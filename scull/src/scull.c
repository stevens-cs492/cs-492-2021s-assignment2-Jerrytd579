#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <wait.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "scull.h"

#define CDEV_NAME "/dev/scull"

/* Number of processes */
;
static int num_processes;
/* Number of threads */
;
static int num_threads;
/* Quantum command line option */
;
static int g_quantum;

/* Thread mutex */
pthread_mutex_t lock;

/* Thread arg structure */
struct thread_args {
	int fd;
	struct task_info t;
};

static void usage(const char *cmd)
{
	printf("Usage: %s <command>\n"
	       "Commands:\n"
	       "  R          Reset quantum\n"
	       "  S <int>    Set quantum\n"
	       "  T <int>    Tell quantum\n"
	       "  G          Get quantum\n"
	       "  Q          Qeuery quantum\n"
	       "  X <int>    Exchange quantum\n"
	       "  H <int>    Shift quantum\n"
	       "  h          Print this message\n"
	       // Added thing to help message
	       "  K	     Custom new quantum thing\n"
	       "  p <int>    Custom new p thing. Range: 0-11\n"
	       "  t <int>    Custom new t thing. Range: 0-11\n"
	       ,
	       cmd);
}

typedef int cmd_t;

static cmd_t parse_arguments(int argc, const char **argv)
{
	cmd_t cmd;

	if (argc < 2) {
		fprintf(stderr, "%s: Invalid number of arguments\n", argv[0]);
		cmd = -1;
		goto ret;
	}

	/* Parse command and optional int argument */
	cmd = argv[1][0];
	switch (cmd) {
	case 'S':
	case 'T':
	case 'H':
	case 'X':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing quantum\n", argv[0]);
			cmd = -1;
			break;
		}
		g_quantum = atoi(argv[2]);
		break;
	case 'R':
	case 'G':
	case 'Q':
	// Added case K
	case 'K':
	// Added case p
	case 'p':
		if (argc < 3) {
			fprintf(stderr, "%s: Missing argument \n", argv[0]);
			cmd = -1;
			break;
		}
		num_processes = atoi(argv[2]);
		break;

	// Added case t
	case 't':
		if (argc < 3){
			fprintf(stderr, "%s: Missing argument \n", argv[0]);
			cmd = -1;
			break;
		}
		num_threads = atoi(argv[2]);
		break;

	case 'h':
		break;
	default:
		fprintf(stderr, "%s: Invalid command\n", argv[0]);
		cmd = -1;
	}

ret:
	if (cmd < 0 || cmd == 'h') {
		usage(argv[0]);
		exit((cmd == 'h')? EXIT_SUCCESS : EXIT_FAILURE);
	}
	return cmd;
};

/* Function that will call ioctl k commands, with threads */
void *startK(void *ptr){
	int retval;
	struct task_info t;
	int fd = *(int*)(ptr);

	// Lock the mutex
	retval = pthread_mutex_lock(&lock);
	if(retval != 0){
		fprintf(stderr, "Warning: mutex cannot be locked. %s.\n", strerror(retval));
	}

	// Critical section
	retval = ioctl(fd, SCULL_IOCKQUANTUM, &t);
	printf("state %ld, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n", t.state, t.stack, t.cpu, t.prio, t.static_prio, t.normal_prio, t.rt_priority, t.pid, t.tgid, t.nvcsw, t.nivcsw);

	// Unlock the mutex
	retval = pthread_mutex_unlock(&lock);
	if(retval != 0){
		fprintf(stderr, "Warning: mutex cannot be unlocked. %s.\n", strerror(retval));
		pthread_exit(NULL);
	}

	pthread_exit(NULL);
}

static int do_op(int fd, cmd_t cmd)
{
	int ret, q;
	struct task_info t;
	_Bool child;
	int i;
	pthread_t *threads;
	int retval;
	int num_started = 0;
	struct thread_args targs[2];

	switch (cmd) {
	case 'R':
		ret = ioctl(fd, SCULL_IOCRESET);
		if (ret == 0)
			printf("Quantum reset\n");
		break;
	case 'Q':
		q = ioctl(fd, SCULL_IOCQQUANTUM);
		printf("Quantum: %d\n", q);
		ret = 0;
		break;
	case 'G':
		ret = ioctl(fd, SCULL_IOCGQUANTUM, &q);
		if (ret == 0)
			printf("Quantum: %d\n", q);
		break;
	case 'T':
		ret = ioctl(fd, SCULL_IOCTQUANTUM, g_quantum);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'S':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCSQUANTUM, &q);
		if (ret == 0)
			printf("Quantum set\n");
		break;
	case 'X':
		q = g_quantum;
		ret = ioctl(fd, SCULL_IOCXQUANTUM, &q);
		if (ret == 0)
			printf("Quantum exchanged, old quantum: %d\n", q);
		break;
	case 'K':
		// Added custom case K
		ret = ioctl(fd, SCULL_IOCKQUANTUM, &t);
		if(ret == 0)
			printf("PID: %d\n", t.pid);
		break;
	case 'p':
		// Added custom case p that uses <n> proceses
		child = false;	
		// Error handling
		if(num_processes < 0){
			fprintf(stderr, "Error: num processes < 0");
			ret = -1;
			break;
		}
		if(num_processes > 11){
			fprintf(stderr, "Error: num processes > 11");
			ret = -1;
			break;
		}
		// Forking the processes
		for(i = 0; i < num_processes; i++){
			pid_t pid = fork();
			if(pid < 0){
				ret = -1;
				break;
			}
			else if(pid == 0){
				child = true;
				break;
			}
		}
		if(child){
			ret = ioctl(fd, SCULL_IOCKQUANTUM, &t);
			printf("state %li, stack %p, cpu %u, prio %d, sprio %d, nprio %d, rtprio %u, pid %d, tgid %d, nv %lu, niv %lu\n",targs->t.state, targs->t.stack, targs->t.cpu, targs->t.prio, targs->t.static_prio, targs->t.normal_prio, targs->t.rt_priority, targs->t.pid, targs->t.tgid, targs->t.nvcsw, targs->t.nivcsw);
			exit(ret);
		}
	       	else{
		// Waiting for the child processes
			for(i = 0; i< num_processes; i++){
				wait(NULL);
		}
	}
		
		break;
	case 't':// Added custom case t that uses <n> threads
		// Creates an array of threads
		threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
		if(threads == NULL){// If threads were not allocated, throw
			ret = -1;
			break;
			}
		// Initialize the mutex
		retval = pthread_mutex_init(&lock, NULL);
		if(retval != 0){
			// If mutex cannot be created, throw
			ret = -1;
			break;
		}
		// Create independent threads and assigns them to array of thread nums
		for(i = 0; i < num_threads; i++, num_started++){
			// If thread cannot be created, throw
			retval = pthread_create(&threads[i], NULL, startK,(void *) &fd);
			if(retval != 0){
				ret = -1;
				break;
			}
		}

		for(i = 0; i < num_started; i++){
			// If threads cannot join, throw
			if(pthread_join(threads[i], NULL) != 0){
				fprintf(stderr, "Error, thread did not join properly");
				ret = -1;
				break;
			}
		}
		// Frees the thread
		free(threads);
		break;
	case 'H':
		q = ioctl(fd, SCULL_IOCHQUANTUM, g_quantum);
		printf("Quantum shifted, old quantum: %d\n", q);
		ret = 0;
		break;
	default:
		/* Should never occur */
		abort();
		ret = -1; /* Keep the compiler happy */
	}
	
	if (ret != 0)
		perror("ioctl");
	return ret;
}

int main(int argc, const char **argv)
{
	int fd, ret;
	cmd_t cmd;

	cmd = parse_arguments(argc, argv);

	fd = open(CDEV_NAME, O_RDONLY);
	if (fd < 0) {
		perror("cdev open");
		return EXIT_FAILURE;
	}

	printf("Device (%s) opened\n", CDEV_NAME);

	ret = do_op(fd, cmd);

	if (close(fd) != 0) {
		perror("cdev close");
		return EXIT_FAILURE;
	}

	printf("Device (%s) closed\n", CDEV_NAME);

	return (ret != 0)? EXIT_FAILURE : EXIT_SUCCESS;
}
