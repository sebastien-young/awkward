#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <netinet/ip.h>

#include <arpa/inet.h>

#define T_LOAD 5
#define QUEUE 3

#define IS_UPPER(c) (c >= 'A' && c <= 'Z')

int setupsocket(int);
void *accept_thread(void *);

int sd = -1;
pthread_mutex_t sd_lock;
void setsd(int fd){
	
	pthread_mutex_lock(&sd_lock);
	sd = fd;
	pthread_mutex_unlock(&sd_lock);
	
}
int getsd(){
	
	int r;
	pthread_mutex_lock(&sd_lock);
	r = sd;
	pthread_mutex_unlock(&sd_lock);
	return r;

}

pthread_t thread[T_LOAD];
pthread_mutex_t t_lock[T_LOAD];

int findthread(){

	int t = 0;
	while(t < T_LOAD && pthread_mutex_trylock(t_lock + t)) t++;
	if(t == T_LOAD)
		t = -1;
	
	return t;
}

int t_done = 0;
pthread_mutex_t done_lock;
int getdone(int t){

	int r;
	pthread_mutex_lock(&done_lock);
	r = t_done;
	t_done = t;
	pthread_mutex_unlock(&done_lock);
	return r;
}

struct pthread_data_t{
	int t_num;
	int c_fd;
	int c_size;
	struct sockaddr_in c_addr;
};

struct pthread_data_t *newthreaddata(){
	
	struct pthread_data_t *t_data = calloc(1, sizeof(struct pthread_data_t));
	t_data->c_size = sizeof(t_data->c_addr);
	return t_data;
}

void send404(char *fname, int fd){
	write(fd, "<html><head>\
		<title>404 Not Found</title>\
		</head><body>\
		<h1>Not Found</h1>\
		<p>The requested URL /", 101);
	write(fd, fname, strlen(fname));
	write(fd, " was not found on this server.</p>\
		</body></html>", 48);
}

char *getclientip(struct sockaddr_in client){
	int size = sizeof(client);
	char *buf = calloc(size, sizeof(char));
	snprintf(buf, size, "%d.%d.%d.%d",
  		(int)(client.sin_addr.s_addr&0xFF),
  		(int)((client.sin_addr.s_addr&0xFF00)>>8),
  		(int)((client.sin_addr.s_addr&0xFF0000)>>16),
 		(int)((client.sin_addr.s_addr&0xFF000000)>>24));
	return buf;
}

void ondeath(int x){
	shutdown(sd, SHUT_RDWR);
	exit(0);
}

void closethreads(int x){
	fprintf(stderr, "%d:broken pipe\n", x);
	int i;
	for(i = 0; i < T_LOAD; i++){
		fprintf(stderr, "%d:clearing thread\n", i);
		if(!pthread_cancel(thread[i])){
			fprintf(stderr, "%d:thread cleared\n", i);
		}else
			fprintf(stderr, "%d:thread not in use\n", i);
	}
}

int main(){
	
	pthread_mutex_init(&sd_lock, NULL);
	setsd(setupsocket(80));
	signal(SIGINT, ondeath);
	signal(SIGPIPE, closethreads);
	
	int i;
	for(i = 0; i < T_LOAD; i++)
		pthread_mutex_init(t_lock + i, NULL);
		
	pthread_mutex_init(&done_lock, NULL);
	
	struct pthread_data_t *t_data = newthreaddata();

	while(1){
		int *r;
   		
		if(listen(getsd(), QUEUE))
			printf("%d:listen error\n", errno);
   
		if((t_data->c_fd = accept(getsd(), 
			(struct sockaddr *) &t_data->c_addr, 
			(socklen_t *) &(t_data->c_size))) == -1)
			printf("%d:accept error\n", errno);
			
		if((t_data->t_num = getdone(-1)) > -1){
			pthread_join(thread[t_data->t_num], (void **) &r);
			pthread_mutex_unlock(t_lock + t_data->t_num);
		}else if(!(t_data->t_num = findthread())) continue;	
		pthread_create(thread + t_data->t_num, NULL, accept_thread, t_data);
		t_data = newthreaddata();
		
	}
	
	close(sd);
	return 0;
}

void endconnection(void *arg){
	struct pthread_data_t *t_data = *(struct pthread_data_t **) arg;
   	close(t_data->c_fd);
   
   int *r = calloc(1, sizeof(int));
   int t;
   if((t = getdone(t_data->t_num)) > -1){
		pthread_join(thread[t], (void **)&r);
		pthread_mutex_unlock(t_lock + t);
   }
   //*r += + 1;
   free(r);
   free(t_data);
   
}

void closefile(void *arg){
	int fd = *(int *) arg;
	close(fd);
}

void *accept_thread(void *arg){

	struct pthread_data_t *t_data = (struct pthread_data_t *) arg;
	pthread_cleanup_push(endconnection, &t_data);
	char *client_ip = getclientip(t_data->c_addr);

	char buffer[512];
	if(read(t_data->c_fd, buffer, 511) == -1)
	printf("%d:read error\n", errno);

	buffer[511] = '\0';
   
	char *fname;
   
   if(!strncmp(buffer, "GET", 3)){
   		fname = buffer + 5;
		if(*fname == ' ')
   			fname = "index.html";
   	
   		char *space = strchr(fname, ' ');
   		if(space) *space = '\0';
   	
		printf("%s:request %s\n", client_ip, fname);

		int fd;
		if((fd = open(fname, O_RDWR)) == -1){
			fprintf(stderr, "404:file not found\n");
			send404(fname, t_data->c_fd);
		}else{
			pthread_cleanup_push(closefile, &fd);
   			char b;
   			struct stat temp;
   			stat(fname, &temp);
	   		int fsize = temp.st_size;
   			while(fsize--){
				read(fd, &b, 1);
				write(t_data->c_fd, &b, 1);
   			}
   			close(fd);
			pthread_cleanup_pop(1);
   		}
   	}
	pthread_cleanup_pop(1);
	return NULL;
}

int setupsocket(int port){

   struct sockaddr_in *sock = calloc(1, sizeof(struct sockaddr_in));
   sock->sin_family = AF_INET;
   sock->sin_port = htons(port);
   sock->sin_addr.s_addr = INADDR_ANY;

   int fd;
   if((fd = socket(sock->sin_family, SOCK_STREAM, 0)) == -1)
		printf("%d:socket error\n", errno);

   if(bind(fd, (struct sockaddr *) sock, sizeof(*sock)))
   		printf("%d:bind error\n", errno);
	
	return(fd);

}
